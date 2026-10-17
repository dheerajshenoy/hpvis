#include "theoryspectrum.hpp"

#include "classwrap.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>
#include <mutex>
#include <numbers>
#include <stdexcept>

QString
TheorySource::label() const
{
    return preset == TheoryPreset::Planck2018
               ? (cosmo.isDefault() ? QString("Planck 2018 best fit")
                                    : QString("LCDM (CLASS, custom parameters)"))
               : QString("Theory: %1").arg(QFileInfo(file).completeBaseName());
}

static QString
cachePath(const CosmoParams &p)
{
    // Part of the key, so a change of CLASS settings does not reuse old files
    QString key = "v2";
    for (const CosmoField &f : cosmoFields)
        key += QString("|%1").arg(p.*f.member, 0, 'g', 10);
    key += QString("|%1|%2").arg(p.ntConsistency).arg(p.yheFromBbn);
    const QByteArray hash
        = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1)
              .toHex()
              .left(16);
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
           + "/theory/class_" + hash + ".txt";
}

bool
theoryTableCached(const CosmoParams &params)
{
    return params.isDefault() || QFile::exists(cachePath(params));
}

QString
ensureTheoryTable(const CosmoParams &params)
{
    const QString path = cachePath(params);
    static std::mutex mutex; // one CLASS run at a time
    std::lock_guard<std::mutex> lock(mutex);
    if (QFile::exists(path))
        return path;

    const ClassSpectra s = computeClassSpectra(params);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(("cannot write " + path).toStdString());
    out.write("#    L    TT             TE             EE             BB\n");
    for (int l = 2; l <= s.lmax; ++l)
        out.write(QString("%1 %2 %3 %4 %5\n")
                      .arg(l)
                      .arg(s.TT[l], 0, 'e', 8)
                      .arg(s.TE[l], 0, 'e', 8)
                      .arg(s.EE[l], 0, 'e', 8)
                      .arg(s.BB[l], 0, 'e', 8)
                      .toUtf8());
    if (!out.commit())
        throw std::runtime_error(("cannot write " + path).toStdString());
    return path;
}

Spectra
loadTheorySpectra(const TheorySource &src)
{
    const QString path
        = src.preset == TheoryPreset::Planck2018
              ? (src.cosmo.isDefault()
                     ? QString(":/resources/theory/planck2018_bestfit.txt")
                     : ensureTheoryTable(src.cosmo))
              : src.file;
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        throw std::runtime_error(
            path.isEmpty() ? "no theory spectrum file is chosen"
                           : ("cannot read " + path).toStdString());

    static const QRegularExpression sep("[,;\\s]+");
    static const QStringList known = {"TT", "EE", "BB", "TE", "TB", "EB"};

    // column of each of TT EE BB TE TB EB (-1 = none); default L TT TE EE BB
    int col[6] = {1, 3, 4, 2, -1, -1};
    QList<QList<double>> rows;
    long lmax = 0;
    while (!file.atEnd())
    {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty())
            continue;
        const bool comment = line.startsWith('#');
        const QStringList parts
            = (comment ? line.mid(1) : line).trimmed().split(sep,
                                                              Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;
        bool numeric = true;
        parts[0].toDouble(&numeric);
        if (!numeric)
        {
            // a header: name the columns
            if (rows.isEmpty() && parts.size() > 2)
            {
                int named[6] = {-1, -1, -1, -1, -1, -1};
                bool any     = false;
                for (int c = 1; c < parts.size(); ++c)
                {
                    const int k = known.indexOf(parts[c].toUpper());
                    if (k >= 0 && named[k] < 0)
                    {
                        named[k] = c;
                        any      = true;
                    }
                }
                if (any)
                    std::copy(std::begin(named), std::end(named),
                              std::begin(col));
            }
            continue;
        }
        QList<double> r;
        for (const QString &p : parts)
        {
            bool ok  = false;
            const double v = p.toDouble(&ok);
            r.append(ok ? v : std::nan(""));
        }
        lmax = std::max(lmax, long(std::lround(r[0])));
        rows.append(r);
    }
    if (rows.isEmpty() || lmax < 2)
        throw std::runtime_error("the file has no multipoles above 1");

    // uK^2 -> the maps' unit squared
    const double unit = src.unit == TheoryUnit::MilliKelvin ? 1e-6
                        : src.unit == TheoryUnit::Kelvin    ? 1e-12
                                                            : 1.0;
    Spectra s;
    std::vector<double> *out[6] = {&s.TT, &s.EE, &s.BB, &s.TE, &s.TB, &s.EB};
    for (int k = 0; k < 6; ++k)
        if (col[k] >= 0)
            out[k]->assign(size_t(lmax) + 1, std::nan(""));
    for (const auto &r : rows)
    {
        const long l = std::lround(r[0]);
        if (l < 1)
            continue; // D_l -> C_l needs l >= 1
        const double f
            = 2.0 * std::numbers::pi / (double(l) * double(l + 1)) * unit;
        for (int k = 0; k < 6; ++k)
            if (col[k] >= 0 && col[k] < r.size())
                (*out[k])[size_t(l)] = r[col[k]] * f;
    }
    return s;
}
