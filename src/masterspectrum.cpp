#include "masterspectrum.hpp"

#include "healpix_cxx/alm.h"
#include "healpix_cxx/alm_healpix_tools.h"
#include "healpix_cxx/arr.h"
#include "healpix_cxx/xcomplex.h"
#include "parallel.hpp"

#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>

// Last: namaster.h pulls in <complex.h>, whose `I` macro breaks other headers
#include "namaster.hpp"
#undef I

namespace master
{
namespace
{
using AlmD = Alm<xcomplex<double>>;

// NaMaster's default for the mask: the mode-coupling matrix depends on it
// directly, so it is always transformed with refinement rounds
constexpr int kMaskIterations = 3;

// fn(i) for i in [0, n), spread over the thread pool
template <class Fn>
void
parallelFor(long n, Fn fn)
{
    std::vector<long> indices(size_t(std::max<long>(n, 0)));
    std::iota(indices.begin(), indices.end(), 0L);
    QtConcurrent::blockingMap(indices, [&fn](long &i) { fn(i); });
}

arr<double>
unitWeights(int nside)
{
    arr<double> w(2 * nside);
    w.fill(1.0);
    return w;
}

// map2alm with `iterations` refinement rounds
void
scalarToAlm(const Healpix_Map<double> &map, AlmD &a, int iterations)
{
    const arr<double> w = unitWeights(int(map.Nside()));
    a.SetToZero();
    map2alm(map, a, w, false);
    if (iterations <= 0)
        return;
    Healpix_Map<double> rec(map.Nside(), RING, SET_NSIDE);
    for (int i = 0; i < iterations; ++i)
    {
        alm2map(a, rec);
        parallel::forRange(map.Npix(), [&](long b, long e)
        {
            for (long p = b; p < e; ++p)
                rec[p] = map[p] - rec[p];
        });
        map2alm(rec, a, w, true);
    }
}

// The same for the two components of a spin-s field (Q, U for s = 2)
void
spinToAlm(const Healpix_Map<double> &m1, const Healpix_Map<double> &m2,
          AlmD &a1, AlmD &a2, int spin, int iterations)
{
    const arr<double> w = unitWeights(int(m1.Nside()));
    a1.SetToZero();
    a2.SetToZero();
    map2alm_spin(m1, m2, a1, a2, spin, w, false);
    if (iterations <= 0)
        return;
    Healpix_Map<double> r1(m1.Nside(), RING, SET_NSIDE),
        r2(m1.Nside(), RING, SET_NSIDE);
    for (int i = 0; i < iterations; ++i)
    {
        alm2map_spin(a1, a2, r1, r2, spin);
        parallel::forRange(m1.Npix(), [&](long b, long e)
        {
            for (long p = b; p < e; ++p)
            {
                r1[p] = m1[p] - r1[p];
                r2[p] = m2[p] - r2[p];
            }
        });
        map2alm_spin(r1, r2, a1, a2, spin, w, true);
    }
}

// Cross power spectrum of two sets of a_lm, l = 0..lmax
std::vector<double>
alm2cl(const AlmD &a, const AlmD &b, int lmax)
{
    std::vector<double> cl(size_t(lmax) + 1, 0.0);
    for (int l = 0; l <= lmax; ++l)
    {
        double sum = a(l, 0).real() * b(l, 0).real();
        for (int m = 1; m <= l; ++m)
            sum += 2.0
                   * (a(l, m).real() * b(l, m).real()
                      + a(l, m).imag() * b(l, m).imag());
        cl[size_t(l)] = sum / (2.0 * l + 1.0);
    }
    return cl;
}

void
addAlm(AlmD &dst, const AlmD &src, int lmax)
{
    for (int l = 0; l <= lmax; ++l)
        for (int m = 0; m <= l; ++m)
            dst(l, m) += src(l, m);
}

// (a, b) = (wa * Q + wb * U, wa * U - wb * Q)
void
combine(const Healpix_Map<double> &wa, const Healpix_Map<double> &wb,
        const Healpix_Map<double> &Q, const Healpix_Map<double> &U,
        Healpix_Map<double> &a, Healpix_Map<double> &b)
{
    parallel::forRange(Q.Npix(), [&](long begin, long end)
    {
        for (long p = begin; p < end; ++p)
        {
            a[p] = wa[p] * Q[p] + wb[p] * U[p];
            b[p] = wa[p] * U[p] - wb[p] * Q[p];
        }
    });
}

// Adds the terms that make the E (and/or B) a_lm of the mask-weighted Q, U
// "pure": the contributions of the first and second derivatives of the mask
// (Smith & Zaldarriaga 2007), as in pymaster's NmtField._purify. Q and U are
// the unweighted maps; aE and aB hold the transform of the weighted ones.
void
purify(const Healpix_Map<double> &Q, const Healpix_Map<double> &U,
       const AlmD &maskAlm, int lmax, int iterations, bool pureE, bool pureB,
       AlmD &aE, AlmD &aB, const Hooks &hooks)
{
    const long nside = Q.Nside();
    std::vector<double> f1(size_t(lmax) + 1), f2(f1), f3(f1), f4(f1);
    for (int l = 0; l <= lmax; ++l)
    {
        const double L = l;
        f1[size_t(l)]  = -std::sqrt((L + 1.0) * L);
        const bool ok  = l >= 2;
        f2[size_t(l)]  = ok ? 2.0 / std::sqrt((L + 2.0) * (L - 1.0)) : 0.0;
        f3[size_t(l)]  = ok ? -std::sqrt((L + 2.0) * (L - 1.0)) : 0.0;
        f4[size_t(l)]
            = ok ? 1.0 / std::sqrt((L + 2.0) * (L + 1.0) * L * (L - 1.0)) : 0.0;
    }

    // The mask's gradient, a spin-1 field with only an "E" part
    AlmD wAlm = maskAlm;
    wAlm.ScaleL(f1);
    AlmD none(lmax, lmax);
    none.SetToZero();

    Healpix_Map<double> wa(nside, RING, SET_NSIDE), wb(nside, RING, SET_NSIDE),
        m1(nside, RING, SET_NSIDE), m2(nside, RING, SET_NSIDE);
    AlmD p1(lmax, lmax), p2(lmax, lmax);

    // Spin-1 term
    hooks.stage("Purification: first derivative of the mask");
    alm2map_spin(wAlm, none, wa, wb, 1);
    combine(wa, wb, Q, U, m1, m2);
    spinToAlm(m1, m2, p1, p2, 1, iterations);
    p1.ScaleL(f2);
    p2.ScaleL(f2);
    if (pureE)
        addAlm(aE, p1, lmax);
    if (pureB)
        addAlm(aB, p2, lmax);
    hooks.done();

    // Spin-2 term: the second derivative, transformed as two scalars
    hooks.stage("Purification: second derivative of the mask");
    wAlm.ScaleL(f3);
    alm2map_spin(wAlm, none, wa, wb, 2);
    combine(wa, wb, Q, U, m1, m2);
    scalarToAlm(m1, p1, iterations);
    scalarToAlm(m2, p2, iterations);
    p1.ScaleL(f4);
    p2.ScaleL(f4);
    if (pureE)
        addAlm(aE, p1, lmax);
    if (pureB)
        addAlm(aB, p2, lmax);
    hooks.done();
}

// --- Bandpowers, coupling matrix, decoupling ---

struct Bands
{
    int count = 0;
    int width = 0;
    std::vector<double> ell; // effective multipole of each band
    int first(int b) const
    {
        return 2 + b * width;
    }
};

Bands
makeBands(int lmax, int width)
{
    Bands bands;
    bands.width = std::max(width, 1);
    bands.count = (lmax - 1) / bands.width;
    if (bands.count < 1)
        throw std::runtime_error("lmax is too small for the bandpower width");
    for (int b = 0; b < bands.count; ++b)
        bands.ell.push_back(bands.first(b) + 0.5 * (bands.width - 1));
    return bands;
}

// One non-zero block of a group's mode-coupling matrix: the spectrum `a` of
// the output is coupled to the spectrum `b` of the input by
// sign * xi[l][l'] * (2l' + 1)
struct Block
{
    int a = 0, b = 0;
    double sign             = 1.0;
    const double *const *xi = nullptr; // xi[l][l']
};

// Inverts the n x n matrix (row-major) by Gauss-Jordan elimination with
// partial pivoting. Returns the inverse and the 1-norm condition number.
std::vector<double>
invert(std::vector<double> a, long n, double &condition)
{
    // 1-norm of the original: the largest column sum
    double normA = 0.0;
    for (long j = 0; j < n; ++j)
    {
        double s = 0.0;
        for (long i = 0; i < n; ++i)
            s += std::abs(a[size_t(i * n + j)]);
        normA = std::max(normA, s);
    }

    std::vector<double> inv(size_t(n * n), 0.0);
    for (long i = 0; i < n; ++i)
        inv[size_t(i * n + i)] = 1.0;

    for (long k = 0; k < n; ++k)
    {
        long pivot      = k;
        double pivotAbs = std::abs(a[size_t(k * n + k)]);
        for (long i = k + 1; i < n; ++i)
        {
            const double v = std::abs(a[size_t(i * n + k)]);
            if (v > pivotAbs)
            {
                pivotAbs = v;
                pivot    = i;
            }
        }
        if (pivotAbs <= 1e-14 * normA || pivotAbs == 0.0)
            throw std::runtime_error(
                "The mode-coupling matrix cannot be inverted: the mask "
                "leaves too little sky for these bandpowers. Use wider "
                "bandpowers or a larger mask.");
        if (pivot != k)
            for (long j = 0; j < n; ++j)
            {
                std::swap(a[size_t(k * n + j)], a[size_t(pivot * n + j)]);
                std::swap(inv[size_t(k * n + j)], inv[size_t(pivot * n + j)]);
            }
        const double scale = 1.0 / a[size_t(k * n + k)];
        for (long j = 0; j < n; ++j)
        {
            a[size_t(k * n + j)] *= scale;
            inv[size_t(k * n + j)] *= scale;
        }
        auto eliminate = [&](long i)
        {
            if (i == k)
                return;
            const double f = a[size_t(i * n + k)];
            if (f == 0.0)
                return;
            for (long j = 0; j < n; ++j)
            {
                a[size_t(i * n + j)] -= f * a[size_t(k * n + j)];
                inv[size_t(i * n + j)] -= f * inv[size_t(k * n + j)];
            }
        };
        if (n >= 256)
            parallelFor(n, eliminate);
        else
            for (long i = 0; i < n; ++i)
                eliminate(i);
    }

    double normInv = 0.0;
    for (long j = 0; j < n; ++j)
    {
        double s = 0.0;
        for (long i = 0; i < n; ++i)
            s += std::abs(inv[size_t(i * n + j)]);
        normInv = std::max(normInv, s);
    }
    condition = normA * normInv;
    return inv;
}

// Bandpowers of the pseudo-spectra of one group, and its windows.
// pseudo[i][l] are the ncls coupled spectra; the result has band-major rows
// (index band * ncls + i), like pymaster's decouple_cell.
std::vector<double>
decoupleGroup(const std::vector<Block> &blocks, int ncls, const Bands &bands,
              int lmax, const std::vector<std::vector<double>> &pseudo,
              Windows &windows, double &condition)
{
    const long nb = bands.count;
    const long n  = nb * ncls;
    const long m  = long(lmax + 1) * ncls;
    const double w = 1.0 / bands.width;

    // Output band b1 couples to input multipole l2 through the mean of the
    // coupling over the multipoles l1 of band b1 ("one-sided" matrix)
    std::vector<double> oneSided(size_t(n * m), 0.0);
    parallelFor(nb, [&](long b1)
    {
        for (const Block &blk : blocks)
        {
            double *row = &oneSided[size_t((b1 * ncls + blk.a) * m)];
            for (int l1 = bands.first(int(b1));
                 l1 < bands.first(int(b1)) + bands.width; ++l1)
            {
                const double *xi = blk.xi[l1];
                for (int l2 = 0; l2 <= lmax; ++l2)
                    row[l2 * ncls + blk.b]
                        += blk.sign * xi[l2] * (2.0 * l2 + 1.0) * w;
            }
        }
    });

    // The binned matrix sums the one-sided one over the multipoles of the
    // input band
    std::vector<double> binned(size_t(n * n), 0.0);
    parallelFor(n, [&](long row)
    {
        const double *src = &oneSided[size_t(row * m)];
        double *dst       = &binned[size_t(row * n)];
        for (long b3 = 0; b3 < nb; ++b3)
            for (int l3 = bands.first(int(b3));
                 l3 < bands.first(int(b3)) + bands.width; ++l3)
                for (int c = 0; c < ncls; ++c)
                    dst[b3 * ncls + c] += src[l3 * ncls + c];
    });

    const std::vector<double> inverse = invert(binned, n, condition);

    // Windows: inverse x one-sided
    windows.ncls   = ncls;
    windows.nbands = int(nb);
    windows.lmax   = lmax;
    windows.data.assign(size_t(n * m), 0.0);
    parallelFor(n, [&](long i)
    {
        double *out = &windows.data[size_t(i * m)];
        for (long k = 0; k < n; ++k)
        {
            const double f = inverse[size_t(i * n + k)];
            if (f == 0.0)
                continue;
            const double *src = &oneSided[size_t(k * m)];
            for (long j = 0; j < m; ++j)
                out[j] += f * src[j];
        }
    });

    // Binned pseudo-spectra, then the solution
    std::vector<double> rhs(size_t(n), 0.0);
    for (long b = 0; b < nb; ++b)
        for (int i = 0; i < ncls; ++i)
        {
            double s = 0.0;
            for (int l = bands.first(int(b));
                 l < bands.first(int(b)) + bands.width; ++l)
                s += pseudo[size_t(i)][size_t(l)];
            rhs[size_t(b * ncls + i)] = s * w;
        }
    std::vector<double> out(size_t(n), 0.0);
    for (long i = 0; i < n; ++i)
    {
        double s = 0.0;
        for (long k = 0; k < n; ++k)
            s += inverse[size_t(i * n + k)] * rhs[size_t(k)];
        out[size_t(i)] = s;
    }
    return out;
}

// The coefficients of the mode-coupling matrix for fields of spins s1 and s2
// (a thin owner around NaMaster's C struct)
struct Coefficients
{
    nmt_master_calculator *c = nullptr;
    Coefficients(int lmax, std::vector<double> &pcl, int s1, int s2,
                 bool pureAny)
    {
        double *pcls[1] = {pcl.data()};
        c = nmt_compute_master_coefficients(lmax, lmax, 1, pcls, s1, s2,
                                            pureAny ? 1 : 0, 0, -1, -1, -1);
        if (!c)
            throw std::runtime_error(
                "NaMaster could not compute the coupling coefficients");
    }
    ~Coefficients()
    {
        if (c)
            nmt_master_calculator_free(c);
    }
    Coefficients(const Coefficients &) = delete;
    Coefficients &operator=(const Coefficients &) = delete;
};

void
checkApodization(long npix, Apodization type, double scaleDeg)
{
    if (scaleDeg <= 0.0 || scaleDeg > 90.0)
        throw std::runtime_error("The apodization scale must be between 0 "
                                 "and 90 degrees");
    const double factor = type == Apodization::Smooth ? 2.5 : 1.2;
    const double rad    = scaleDeg * M_PI / 180.0;
    // NaMaster stops the program for a scale smaller than a pixel: say so
    // first
    if (4.0 * double(npix) * (1.0 - std::cos(factor * rad)) < 2.0)
        throw std::runtime_error(
            "The apodization scale is smaller than a pixel of this map");
}
} // namespace

int
stageCount(const Options &o, bool polarized)
{
    const bool apodize = o.apodization != Apodization::None;
    const bool pure    = polarized && (o.purifyE || o.purifyB);
    return (apodize ? 1 : 0)    // apodization
           + 1                  // transform of the mask
           + 1                  // T
           + (polarized ? 1 : 0)  // Q, U
           + (pure ? 2 : 0)     // purification
           + 1                  // TT group
           + (polarized ? 2 : 0); // TE and EE groups
}

Result
compute(const Input &input, const Options &options, const Hooks &hooks)
{
    const int lmax = options.lmax;
    if (lmax < 4)
        throw std::runtime_error("lmax is too small");
    const bool pol = input.polarized;
    const bool pureE = pol && options.purifyE;
    const bool pureB = pol && options.purifyB;
    const bool pure  = pureE || pureB;
    if (pure && options.apodization == Apodization::None)
        throw std::runtime_error(
            "Purification needs an apodized mask (it uses the derivatives of "
            "the mask)");

    const Bands bands = makeBands(lmax, options.bandwidth);
    const long nside  = input.T.Nside();
    const long npix   = input.T.Npix();

    hooks.total(stageCount(options, pol));

    // --- The weights ---
    Healpix_Map<double> w = input.weights;
    if (options.apodization != Apodization::None)
    {
        hooks.stage("Apodizing the mask");
        checkApodization(npix, options.apodization,
                         options.apodizationScale);
        Healpix_Map<double> apodized(nside, RING, SET_NSIDE);
        char type[8];
        std::strcpy(type, options.apodization == Apodization::C1   ? "C1"
                          : options.apodization == Apodization::C2 ? "C2"
                                                                   : "Smooth");
        nmt_apodize_mask(nside, const_cast<double *>(w.Map().begin()),
                         const_cast<double *>(apodized.Map().begin()),
                         options.apodizationScale, type);
        w.swap(apodized);
        hooks.done();
    }

    Result result;
    result.lmax      = lmax;
    result.bandwidth = bands.width;
    result.ell       = bands.ell;
    result.purifyE   = pureE;
    result.purifyB   = pureB;
    {
        double s2 = 0.0;
        for (long p = 0; p < npix; ++p)
            s2 += w[p] * w[p];
        result.fsky = s2 / double(npix);
        if (result.fsky <= 0.0)
            throw std::runtime_error("The mask removes the whole sky");
    }

    // --- The mask's own spectrum ---
    hooks.stage("Transform of the mask");
    std::vector<double> pclMask;
    AlmD maskAlm(lmax, lmax);
    {
        scalarToAlm(w, maskAlm, kMaskIterations);
        pclMask = alm2cl(maskAlm, maskAlm, lmax);
        for (int l = 0; l <= lmax; ++l)
            pclMask[size_t(l)] *= (2.0 * l + 1.0) / (4.0 * M_PI);
    }
    hooks.done();

    // --- The weighted fields ---
    auto weigh = [&](Healpix_Map<double> &map)
    {
        parallel::forRange(npix, [&](long b, long e)
        {
            for (long p = b; p < e; ++p)
                map[p] *= w[p];
        });
    };

    hooks.stage("Transform of the temperature");
    AlmD aT(lmax, lmax);
    {
        Healpix_Map<double> t = input.T;
        weigh(t);
        scalarToAlm(t, aT, options.iterations);
    }
    hooks.done();

    AlmD aE(lmax, lmax), aB(lmax, lmax);
    if (pol)
    {
        hooks.stage("Transform of Q and U");
        {
            Healpix_Map<double> q = input.Q, u = input.U;
            weigh(q);
            weigh(u);
            spinToAlm(q, u, aE, aB, 2, options.iterations);
        }
        hooks.done();
        if (pure)
            purify(input.Q, input.U, maskAlm, lmax, options.iterations, pureE,
                   pureB, aE, aB, hooks);
    }

    // --- Pseudo-spectra, mode-coupling matrices and decoupling ---
    double worst = 1.0;

    {
        hooks.stage("Coupling matrix (TT)");
        const std::vector<double> clTT = alm2cl(aT, aT, lmax);
        Coefficients coeffs(lmax, pclMask, 0, 0, false);
        std::vector<Block> blocks
            = {{0, 0, 1.0, coeffs.c->xi_00[0]}};
        double cond = 1.0;
        const auto x = decoupleGroup(blocks, 1, bands, lmax, {clTT},
                                     result.windowsTT, cond);
        worst        = std::max(worst, cond);
        result.TT.assign(x.begin(), x.end());
        hooks.done();
    }

    if (pol)
    {
        {
            hooks.stage("Coupling matrix (TE)");
            const std::vector<double> clTE = alm2cl(aT, aE, lmax);
            const std::vector<double> clTB = alm2cl(aT, aB, lmax);
            Coefficients coeffs(lmax, pclMask, 0, 2, pure);
            // With purification the sets are indexed by the number of
            // purified fields: the second field is the polarization
            const int e = pureE ? 1 : 0;
            const int b = pureB ? 1 : 0;
            std::vector<Block> blocks
                = {{0, 0, 1.0, coeffs.c->xi_0s[0][e]},
                   {1, 1, 1.0, coeffs.c->xi_0s[0][b]}};
            double cond = 1.0;
            const auto x = decoupleGroup(blocks, 2, bands, lmax,
                                         {clTE, clTB}, result.windowsTE, cond);
            worst        = std::max(worst, cond);
            for (int i = 0; i < bands.count; ++i)
            {
                result.TE.push_back(x[size_t(i) * 2]);
                result.TB.push_back(x[size_t(i) * 2 + 1]);
            }
            hooks.done();
        }
        {
            hooks.stage("Coupling matrix (EE, EB, BB)");
            const std::vector<double> clEE = alm2cl(aE, aE, lmax);
            const std::vector<double> clEB = alm2cl(aE, aB, lmax);
            const std::vector<double> clBB = alm2cl(aB, aB, lmax);
            Coefficients coeffs(lmax, pclMask, 2, 2, pure);
            const int e = pureE ? 1 : 0;
            const int b = pureB ? 1 : 0;
            const auto pp = coeffs.c->xi_pp[0];
            const auto mm = coeffs.c->xi_mm[0];
            std::vector<Block> blocks = {
                {0, 0, 1.0, pp[2 * e]},     {1, 1, 1.0, pp[e + b]},
                {2, 2, 1.0, pp[e + b]},     {3, 3, 1.0, pp[2 * b]},
                {0, 3, 1.0, mm[2 * e]},     {1, 2, -1.0, mm[e + b]},
                {2, 1, -1.0, mm[e + b]},    {3, 0, 1.0, mm[2 * b]},
            };
            double cond = 1.0;
            // EB and BE are the same spectrum for a single field
            const auto x
                = decoupleGroup(blocks, 4, bands, lmax,
                                {clEE, clEB, clEB, clBB}, result.windowsEE,
                                cond);
            worst = std::max(worst, cond);
            for (int i = 0; i < bands.count; ++i)
            {
                result.EE.push_back(x[size_t(i) * 4]);
                result.EB.push_back(x[size_t(i) * 4 + 1]);
                result.BB.push_back(x[size_t(i) * 4 + 3]);
            }
            hooks.done();
        }
    }

    result.conditionNumber = worst;
    return result;
}

Result
binTheory(const Result &c, const TheoryCl &th)
{
    Result out;
    out.ell       = c.ell;
    out.lmax      = c.lmax;
    out.bandwidth = c.bandwidth;

    auto at = [](const std::vector<double> &v, int l)
    {
        return size_t(l) < v.size() ? v[size_t(l)] : 0.0;
    };

    // out[row] = sum_j windows[row][j] * x[j]
    auto apply = [](const Windows &win, const std::vector<double> &x,
                    int spectrum)
    {
        std::vector<double> bands(size_t(win.nbands), 0.0);
        const long m = long(win.lmax + 1) * win.ncls;
        for (int b = 0; b < win.nbands; ++b)
        {
            const double *row
                = &win.data[size_t((long(b) * win.ncls + spectrum) * m)];
            double s = 0.0;
            for (long j = 0; j < m; ++j)
                s += row[j] * x[size_t(j)];
            bands[size_t(b)] = s;
        }
        return bands;
    };

    if (c.windowsTT.nbands > 0)
    {
        std::vector<double> x(size_t(c.lmax) + 1);
        for (int l = 0; l <= c.lmax; ++l)
            x[size_t(l)] = at(th.TT, l);
        out.TT = apply(c.windowsTT, x, 0);
    }
    if (c.windowsTE.nbands > 0)
    {
        std::vector<double> x(size_t(c.lmax + 1) * 2, 0.0);
        for (int l = 0; l <= c.lmax; ++l)
            x[size_t(l) * 2] = at(th.TE, l);
        out.TE = apply(c.windowsTE, x, 0);
        out.TB = apply(c.windowsTE, x, 1);
    }
    if (c.windowsEE.nbands > 0)
    {
        std::vector<double> x(size_t(c.lmax + 1) * 4, 0.0);
        for (int l = 0; l <= c.lmax; ++l)
        {
            x[size_t(l) * 4]     = at(th.EE, l);
            x[size_t(l) * 4 + 3] = at(th.BB, l);
        }
        out.EE = apply(c.windowsEE, x, 0);
        out.EB = apply(c.windowsEE, x, 1);
        out.BB = apply(c.windowsEE, x, 3);
    }
    return out;
}
} // namespace master
