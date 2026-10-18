#include "datatransform.hpp"

#include "parallel.hpp"
#include "unseen.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace datatransform
{
QString
label(Kind kind)
{
    switch (kind)
    {
    case Kind::Log: return "Logarithm";
    case Kind::Asinh: return "Inverse hyperbolic sine";
    case Kind::Atan: return "Arctangent";
    case Kind::Tanh: return "Hyperbolic tangent";
    case Kind::Power: return "Power law";
    case Kind::Exp: return "Exponential";
    case Kind::HistogramEqualize: return "Histogram equalization";
    case Kind::Standardize: return "Normalize (mean 0, std 1)";
    case Kind::MinMax: return "Normalize (0 to 1)";
    default: return {};
    }
}

QString
formula(Kind kind)
{
    switch (kind)
    {
    case Kind::Log: return "log10((x - mu) / sigma), only where positive";
    case Kind::Asinh: return "asinh((x - mu) / sigma)";
    case Kind::Atan: return "atan((x - mu) / sigma)";
    case Kind::Tanh: return "tanh((x - mu) / sigma)";
    case Kind::Power:
        return "sign(u) |u|^p with u = (x - mu) / sigma";
    case Kind::Exp: return "exp((x - mu) / sigma)";
    case Kind::HistogramEqualize:
        return "the rank of x among the valid pixels, from 0 to 1";
    case Kind::Standardize: return "(x - mean) / std of the valid pixels";
    case Kind::MinMax: return "(x - min) / (max - min) of the valid pixels";
    default: return {};
    }
}

bool
usesMu(Kind kind)
{
    return kind != Kind::HistogramEqualize && kind != Kind::Standardize
           && kind != Kind::MinMax;
}

bool
usesSigma(Kind kind)
{
    return usesMu(kind);
}

bool
usesPower(Kind kind)
{
    return kind == Kind::Power;
}

void
meanStd(const Healpix_Map<float> &map, double &mean, double &std)
{
    double sum = 0.0, sum2 = 0.0;
    long n = 0;
    for (int i = 0; i < map.Npix(); ++i)
        if (isSeen(map[i]))
        {
            sum += map[i];
            sum2 += double(map[i]) * map[i];
            ++n;
        }
    mean = n ? sum / n : 0.0;
    std  = n ? std::sqrt(std::max(sum2 / n - mean * mean, 0.0)) : 0.0;
}

long
apply(Healpix_Map<float> &map, Kind kind, const Params &p)
{
    const long npix = long(map.Npix());
    float *data     = &map[0];
    std::atomic<long> lost{0};

    // Per-pixel function of the value (given as double), NaN for "no value"
    auto run = [&](auto fn)
    {
        parallel::forRange(npix, [&](long begin, long end)
        {
            long bad = 0;
            for (long i = begin; i < end; ++i)
            {
                if (!isSeen(data[i]))
                {
                    data[i] = UNSEEN;
                    continue;
                }
                const double v = fn(double(data[i]));
                if (std::isfinite(v) && std::abs(v) < 1e30)
                    data[i] = float(v);
                else
                {
                    data[i] = UNSEEN;
                    ++bad;
                }
            }
            lost += bad;
        });
    };

    const double sigma = p.sigma != 0.0 ? p.sigma : 1.0;
    auto u             = [&](double x) { return (x - p.mu) / sigma; };

    switch (kind)
    {
    case Kind::Log:
        run([&](double x)
        {
            const double v = u(x);
            return v > 0.0 ? std::log10(v) : std::nan("");
        });
        break;
    case Kind::Asinh: run([&](double x) { return std::asinh(u(x)); }); break;
    case Kind::Atan: run([&](double x) { return std::atan(u(x)); }); break;
    case Kind::Tanh: run([&](double x) { return std::tanh(u(x)); }); break;
    case Kind::Exp: run([&](double x) { return std::exp(u(x)); }); break;
    case Kind::Power:
        run([&](double x)
        {
            const double v = u(x);
            const double r = std::pow(std::abs(v), p.power);
            return v < 0.0 ? -r : r;
        });
        break;
    case Kind::Standardize:
    {
        double mean, std;
        meanStd(map, mean, std);
        if (std <= 0.0)
            return 0;
        run([&](double x) { return (x - mean) / std; });
        break;
    }
    case Kind::MinMax:
    {
        double lo = 1e300, hi = -1e300;
        for (long i = 0; i < npix; ++i)
            if (isSeen(data[i]))
            {
                lo = std::min(lo, double(data[i]));
                hi = std::max(hi, double(data[i]));
            }
        if (!(hi > lo))
            return 0;
        run([&](double x) { return (x - lo) / (hi - lo); });
        break;
    }
    case Kind::HistogramEqualize:
    {
        std::vector<float> sorted;
        sorted.reserve(size_t(npix));
        for (long i = 0; i < npix; ++i)
            if (isSeen(data[i]))
                sorted.push_back(data[i]);
        if (sorted.size() < 2)
            return 0;
        std::sort(sorted.begin(), sorted.end());
        const double denom = double(sorted.size() - 1);
        // Ties share the mean of their ranks, so a flat region maps to one
        // value in the middle of its span
        run([&](double x)
        {
            const auto range = std::equal_range(sorted.begin(), sorted.end(),
                                                float(x));
            const double lo  = double(range.first - sorted.begin());
            const double hi  = double(range.second - sorted.begin()) - 1.0;
            return 0.5 * (lo + hi) / denom;
        });
        break;
    }
    default: break;
    }
    return lost;
}
} // namespace datatransform
