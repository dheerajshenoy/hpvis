#include "maskbuilder.hpp"

#include "parallel.hpp"
#include "smoothing.hpp"
#include "unseen.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace maskbuilder
{
QString
Rule::summary(SkyFrame frame) const
{
    const QString lon = skyframe::lonName(frame), lat = skyframe::latName(frame);
    QString text;
    switch (type)
    {
    case RuleType::Threshold:
        text = QString("Threshold %1 to %2").arg(lo, 0, 'g', 5).arg(hi, 0, 'g', 5);
        break;
    case RuleType::GalacticCut:
        text = QString("Galactic cut |b| >= %1°").arg(latitude, 0, 'g', 4);
        break;
    case RuleType::Disc:
        text = QString("Disc (%1, %2) = (%3°, %4°), r = %5°, %6")
                   .arg(lon, lat)
                   .arg(this->lon, 0, 'g', 4)
                   .arg(this->lat, 0, 'g', 4)
                   .arg(radius, 0, 'g', 4)
                   .arg(keepInside ? "keep inside" : "cut out");
        break;
    }
    return text;
}

Result
build(const Healpix_Map<float> &map, SkyFrame frame,
      const std::vector<Rule> &rules, double apodFwhmDeg)
{
    Result result;
    const long npix = long(map.Npix());
    const Healpix_Base base(int(map.Nside()), map.Scheme(), SET_NSIDE);

    // Rotation to Galactic for the galactic cut, centre vectors for discs
    const auto toGal = skyframe::rotation(frame, SkyFrame::Galactic);
    const float *m   = &map[0];

    std::vector<uint8_t> keep(size_t(npix), 1);
    bool first = true;
    for (const Rule &rule : rules)
    {
        if (!rule.enabled)
            continue;

        std::vector<uint8_t> r(size_t(npix), 1);
        switch (rule.type)
        {
        case RuleType::Threshold:
            parallel::forRange(npix, [&](long begin, long end)
            {
                for (long i = begin; i < end; ++i)
                    r[size_t(i)] = isSeen(m[i]) && m[i] >= rule.lo
                                   && m[i] <= rule.hi;
            });
            break;
        case RuleType::GalacticCut:
        {
            const double zmax
                = std::sin(rule.latitude * std::numbers::pi / 180.0);
            parallel::forRange(npix, [&](long begin, long end)
            {
                for (long i = begin; i < end; ++i)
                {
                    const vec3 v = base.pix2vec(i);
                    const QVector3D g = skyframe::apply(
                        toGal, QVector3D(float(v.x), float(v.y), float(v.z)));
                    r[size_t(i)] = std::abs(double(g.z())) >= zmax;
                }
            });
            break;
        }
        case RuleType::Disc:
        {
            const double lo = rule.lon * std::numbers::pi / 180.0;
            const double la = rule.lat * std::numbers::pi / 180.0;
            const vec3 c(std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo),
                         std::sin(la));
            const double cosR = std::cos(rule.radius * std::numbers::pi / 180.0);
            parallel::forRange(npix, [&](long begin, long end)
            {
                for (long i = begin; i < end; ++i)
                {
                    const bool inside = dotprod(base.pix2vec(i), c) >= cosR;
                    r[size_t(i)]      = rule.keepInside ? inside : !inside;
                }
            });
            break;
        }
        }

        if (first)
            keep = std::move(r);
        else
            for (long i = 0; i < npix; ++i)
            {
                uint8_t &k = keep[size_t(i)];
                switch (rule.combine)
                {
                case Combine::And: k = k && r[size_t(i)]; break;
                case Combine::Or: k = k || r[size_t(i)]; break;
                case Combine::Subtract: k = k && !r[size_t(i)]; break;
                }
            }
        first = false;
    }

    auto out = std::make_shared<Healpix_Map<float>>(map.Nside(), map.Scheme(),
                                                    SET_NSIDE);
    float *w = &(*out)[0];
    for (long i = 0; i < npix; ++i)
        w[i] = keep[size_t(i)] ? 1.0f : 0.0f;

    if (apodFwhmDeg > 0.0)
    {
        SmoothOptions o;
        o.fwhmRad    = apodFwhmDeg * std::numbers::pi / 180.0;
        o.iterations = 1;
        o.lmax       = int(std::min<long>(3 * map.Nside() - 1,
                                          long(8.0 * 180.0 / apodFwhmDeg) + 64));
        o.keepUnseen = false;
        const SmoothResult s = smoothMap(*out, o);
        if (!s.map)
        {
            result.error = "Apodization failed: " + s.error;
            return result;
        }
        // A straight edge is at 0.5 after smoothing: starting there keeps the
        // mask from growing and the taper reaches 1 inside
        const float *sm = &(*s.map)[0];
        for (long i = 0; i < npix; ++i)
            w[i] = std::clamp(2.0f * sm[i] - 1.0f, 0.0f, 1.0f);
    }

    double s1 = 0.0, s2 = 0.0, s4 = 0.0;
    for (long i = 0; i < npix; ++i)
    {
        const double v = w[i];
        s1 += v;
        s2 += v * v;
        s4 += v * v * v * v;
    }
    result.fsky  = s1 / npix;
    result.fsky2 = s2 / npix;
    result.fsky4 = s4 / npix;
    result.mask  = std::move(out);
    return result;
}
} // namespace maskbuilder
