#include "units.hpp"

#include <cmath>
#include <limits>

namespace units
{
namespace
{
constexpr double kPlanck = 6.62607015e-34;  // J s
constexpr double kBoltzmann = 1.380649e-23; // J / K
constexpr double kLight = 299792458.0;      // m / s

// Rayleigh-Jeans temperature of a thermodynamic (CMB) one: x^2 e^x / (e^x - 1)^2
// with x = h nu / (k T_CMB)
double
cmbToRj(double freqHz)
{
    const double x  = kPlanck * freqHz / (kBoltzmann * kTcmb);
    const double ex = std::expm1(x); // e^x - 1
    return x * x * (ex + 1.0) / (ex * ex);
}

// Jy/sr per K_RJ: 2 k nu^2 / c^2 (in SI), 1 Jy = 1e-26 W / m^2 / Hz
double
rjToJySr(double freqHz)
{
    return 2.0 * kBoltzmann * freqHz * freqHz / (kLight * kLight) * 1e26;
}

// How many K_CMB one unit is
double
inKcmb(Unit u, double freqHz)
{
    switch (u)
    {
        case Unit::K_CMB: return 1.0;
        case Unit::mK_CMB: return 1e-3;
        case Unit::uK_CMB: return 1e-6;
        case Unit::K_RJ: return 1.0 / cmbToRj(freqHz);
        case Unit::mK_RJ: return 1e-3 / cmbToRj(freqHz);
        case Unit::uK_RJ: return 1e-6 / cmbToRj(freqHz);
        case Unit::Jy_sr: return 1.0 / (cmbToRj(freqHz) * rjToJySr(freqHz));
        case Unit::MJy_sr:
            return 1e6 / (cmbToRj(freqHz) * rjToJySr(freqHz));
        case Unit::Count_: break;
    }
    return 1.0;
}
} // namespace

QString
label(Unit unit)
{
    switch (unit)
    {
        case Unit::K_CMB: return "K_CMB";
        case Unit::mK_CMB: return "mK_CMB";
        case Unit::uK_CMB: return "uK_CMB";
        case Unit::K_RJ: return "K_RJ";
        case Unit::mK_RJ: return "mK_RJ";
        case Unit::uK_RJ: return "uK_RJ";
        case Unit::Jy_sr: return "Jy/sr";
        case Unit::MJy_sr: return "MJy/sr";
        case Unit::Count_: break;
    }
    return {};
}

std::optional<Unit>
fromFits(const QString &tunit)
{
    QString t = tunit.trimmed();
    t.remove(' ');
    t.replace(QChar(0x00b5), 'u'); // micro sign
    t.replace(QChar(0x03bc), 'u'); // greek mu
    if (t.startsWith("mu", Qt::CaseInsensitive) && t.size() > 2
        && t[2].toLower() == 'k')
        t.remove(0, 1); // "muK" -> "uK"

    // Brightness: case matters (mega or milli)
    if (t == "MJy/sr" || t == "MJysr-1" || t == "MJy.sr-1" || t == "MJy/sr.")
        return Unit::MJy_sr;
    if (t == "Jy/sr" || t == "Jysr-1" || t == "Jy.sr-1")
        return Unit::Jy_sr;

    const QString l = t.toLower();
    struct Entry
    {
        const char *name;
        Unit unit;
    };
    static const Entry table[] = {
        {"k", Unit::K_CMB},        {"k_cmb", Unit::K_CMB},
        {"kcmb", Unit::K_CMB},     {"k(cmb)", Unit::K_CMB},
        {"mk", Unit::mK_CMB},      {"mk_cmb", Unit::mK_CMB},
        {"mkcmb", Unit::mK_CMB},   {"mk(cmb)", Unit::mK_CMB},
        {"uk", Unit::uK_CMB},      {"uk_cmb", Unit::uK_CMB},
        {"ukcmb", Unit::uK_CMB},   {"uk(cmb)", Unit::uK_CMB},
        {"k_rj", Unit::K_RJ},      {"krj", Unit::K_RJ},
        {"k(rj)", Unit::K_RJ},     {"mk_rj", Unit::mK_RJ},
        {"mkrj", Unit::mK_RJ},     {"mk(rj)", Unit::mK_RJ},
        {"uk_rj", Unit::uK_RJ},    {"ukrj", Unit::uK_RJ},
        {"uk(rj)", Unit::uK_RJ},
    };
    for (const Entry &e : table)
        if (l == QLatin1String(e.name))
            return e.unit;
    return std::nullopt;
}

bool
isCmbTemperature(Unit unit)
{
    return unit == Unit::K_CMB || unit == Unit::mK_CMB
           || unit == Unit::uK_CMB;
}

bool
needsFrequency(Unit from, Unit to)
{
    return !(isCmbTemperature(from) && isCmbTemperature(to));
}

double
factor(Unit from, Unit to, double freqGHz)
{
    if (needsFrequency(from, to) && !(freqGHz > 0.0))
        return std::numeric_limits<double>::quiet_NaN();
    const double hz = freqGHz * 1e9;
    return inKcmb(from, hz) / inKcmb(to, hz);
}
} // namespace units
