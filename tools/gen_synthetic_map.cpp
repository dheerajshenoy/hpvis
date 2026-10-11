// Writes a synthetic full-sky HEALPix map (RING, float32) for stress-testing
// the viewer at large nside. Usage: gen_synthetic_map <nside> <out.fits>
//
// The sky is a smooth dipole + low-order patterns + a few Gaussian blobs, plus
// fine small-scale structure (so zooming in keeps showing detail) and a
// circular patch of UNSEEN pixels.
#include <healpix_cxx/healpix_map.h>
#include <healpix_cxx/healpix_map_fitsio.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

static float
hashNoise(std::uint64_t i)
{
    i ^= i >> 33;
    i *= 0xff51afd7ed558ccdULL;
    i ^= i >> 33;
    i *= 0xc4ceb9fe1a85ec53ULL;
    i ^= i >> 33;
    return float(i & 0xFFFFFF) / float(0xFFFFFF) - 0.5f; // [-0.5, 0.5]
}

static double
angularDistance(double theta1, double phi1, double theta2, double phi2)
{
    const double c = std::cos(theta1) * std::cos(theta2)
                     + std::sin(theta1) * std::sin(theta2)
                           * std::cos(phi1 - phi2);
    return std::acos(std::fmax(-1.0, std::fmin(1.0, c)));
}

int
main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: " << argv[0] << " <nside> <out.fits>\n";
        return 1;
    }
    const int nside = std::atoi(argv[1]);
    Healpix_Map<float> map(nside, RING, SET_NSIDE);
    const long npix = map.Npix();
    std::cout << "nside " << nside << ", " << npix << " pixels\n";

    struct Blob { double theta, phi, sigma, amp; };
    const Blob blobs[] = {{1.0, 0.8, 0.10, 3.0},
                          {2.0, 3.5, 0.05, -2.0},
                          {1.5, 5.2, 0.20, 1.5},
                          {0.6, 2.0, 0.02, 4.0}};
    const double holeTheta = 1.9, holePhi = 4.2, holeRadius = 0.15;

#pragma omp parallel for schedule(static)
    for (long i = 0; i < npix; ++i)
    {
        const pointing p = map.pix2ang(i);
        const double th = p.theta, ph = p.phi;

        if (angularDistance(th, ph, holeTheta, holePhi) < holeRadius)
        {
            map[i] = float(Healpix_undef); // UNSEEN
            continue;
        }

        double v = std::cos(th)                                      // dipole
                   + 0.5 * std::sin(3.0 * ph) * std::pow(std::sin(th), 3)
                   + 0.3 * std::cos(7.0 * ph + 4.0 * th) * std::sin(th);
        for (const Blob &b : blobs)
        {
            const double d = angularDistance(th, ph, b.theta, b.phi);
            v += b.amp * std::exp(-0.5 * d * d / (b.sigma * b.sigma));
        }
        // Detail at many scales, down to a few pixels
        v += 0.15 * std::sin(120.0 * th) * std::sin(120.0 * ph);
        v += 0.05 * std::sin(1500.0 * th + 3.0 * ph);
        v += 0.02 * hashNoise(std::uint64_t(i));

        map[i] = float(v);
    }

    std::cout << "writing " << argv[2] << " ...\n";
    // The leading "!" overwrites an existing file
    write_Healpix_map_to_fits(std::string("!") + argv[2], map, PLANCK_FLOAT32);
    std::cout << "done\n";
    return 0;
}
