#include "colormapregistry.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

namespace
{
const QStringList kBuiltinNames
    = {"Grayscale", "Inferno", "Magma",  "Plasma", "Viridis",
       "Cubehelix", "Jet",     "HSV",    "Hot",    "Cool",
       "Spring",    "Summer",  "Autumn", "Winter", "Rainbow"};

struct Stop
{
    double pos = 0.0;
    double r = 0, g = 0, b = 0;
};

bool
parseHex(const QString &text, Stop &s)
{
    static const QRegularExpression re("^#?([0-9a-fA-F]{6})$");
    const auto m = re.match(text.trimmed());
    if (!m.hasMatch())
        return false;
    const uint v = m.captured(1).toUInt(nullptr, 16);
    s.r          = ((v >> 16) & 0xFF) / 255.0;
    s.g          = ((v >> 8) & 0xFF) / 255.0;
    s.b          = (v & 0xFF) / 255.0;
    return true;
}

// Builds the table from stops, which span [0, 1]
QVector<QRgb>
resample(QVector<Stop> stops)
{
    std::stable_sort(stops.begin(), stops.end(),
                     [](const Stop &a, const Stop &b) { return a.pos < b.pos; });

    QVector<QRgb> lut(ColormapRegistry::kLutSize);
    int seg = 0;
    for (int i = 0; i < lut.size(); ++i)
    {
        const double t = double(i) / (lut.size() - 1);
        while (seg + 2 < stops.size() && t > stops[seg + 1].pos)
            ++seg;
        const Stop &a     = stops[seg];
        const Stop &b     = stops[seg + 1];
        const double span = b.pos - a.pos;
        const double f
            = span > 0 ? std::clamp((t - a.pos) / span, 0.0, 1.0) : 1.0;
        auto mix = [f](double x, double y)
        { return std::clamp(x + (y - x) * f, 0.0, 1.0); };
        lut[i] = qRgb(qRound(mix(a.r, b.r) * 255), qRound(mix(a.g, b.g) * 255),
                      qRound(mix(a.b, b.b) * 255));
    }
    return lut;
}

// Values above 1 mean the file uses 0-255
void
normalise(QVector<Stop> &stops)
{
    double mx = 0;
    for (const Stop &s : stops)
        mx = std::max({mx, s.r, s.g, s.b});
    if (mx > 1.0)
        for (Stop &s : stops)
        {
            s.r /= 255.0;
            s.g /= 255.0;
            s.b /= 255.0;
        }
}

void
spreadEvenly(QVector<Stop> &stops)
{
    for (int i = 0; i < stops.size(); ++i)
        stops[i].pos = stops.size() > 1 ? double(i) / (stops.size() - 1) : 0.0;
}

bool
parseCsv(const QString &text, QVector<Stop> &stops, QString *error)
{
    static const QRegularExpression sep("[,;\\s]+");
    int lineNo = 0;
    for (const QString &raw : text.split('\n'))
    {
        ++lineNo;
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;

        Stop s;
        if (parseHex(line, s))
        {
            stops << s;
            continue;
        }
        if (line.startsWith('#') || line.startsWith("//"))
            continue; // comment

        const QStringList parts = line.split(sep, Qt::SkipEmptyParts);
        bool ok[3]              = {false, false, false};
        double v[3]             = {0, 0, 0};
        for (int i = 0; i < 3 && i < parts.size(); ++i)
            v[i] = parts[i].toDouble(&ok[i]);

        if (parts.size() >= 3 && ok[0] && ok[1] && ok[2])
        {
            s.r = v[0];
            s.g = v[1];
            s.b = v[2];
            stops << s;
        }
        else if (stops.isEmpty())
            continue; // a header line such as "r,g,b"
        else
        {
            if (error)
                *error = QString("line %1: expected r,g,b").arg(lineNo);
            return false;
        }
    }
    normalise(stops);
    spreadEvenly(stops);
    return true;
}

bool
colorFromJson(const QJsonValue &v, Stop &s)
{
    if (v.isString())
        return parseHex(v.toString(), s);
    if (v.isArray())
    {
        const QJsonArray a = v.toArray();
        if (a.size() < 3)
            return false;
        s.r = a[0].toDouble();
        s.g = a[1].toDouble();
        s.b = a[2].toDouble();
        return true;
    }
    return false;
}

bool
parseJson(const QByteArray &data, QString &name, QVector<Stop> &stops,
          QString *error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (doc.isNull())
    {
        if (error)
            *error = "invalid JSON: " + pe.errorString();
        return false;
    }

    QJsonArray colors;
    if (doc.isArray())
        colors = doc.array();
    else
    {
        const QJsonObject o = doc.object();
        name                = o.value("name").toString();
        colors              = o.value("colors").toArray();
    }

    bool anyPos = false, allPos = true;
    for (const QJsonValue &c : colors)
    {
        Stop s;
        if (c.isObject())
        {
            const QJsonObject o = c.toObject();
            if (!colorFromJson(o.value("color"), s))
            {
                if (error)
                    *error = "a stop has no valid \"color\"";
                return false;
            }
            if (o.contains("position"))
            {
                s.pos  = o.value("position").toDouble();
                anyPos = true;
            }
            else
                allPos = false;
        }
        else if (colorFromJson(c, s))
            allPos = false;
        else
        {
            if (error)
                *error = "colors must be \"#rrggbb\", [r, g, b] or "
                         "{\"position\", \"color\"}";
            return false;
        }
        stops << s;
    }

    if (anyPos && !allPos)
    {
        if (error)
            *error = "either all colors have a position or none do";
        return false;
    }
    normalise(stops);
    if (!anyPos)
        spreadEvenly(stops);
    return true;
}
} // namespace

ColormapRegistry::ColormapRegistry() = default;

ColormapRegistry &
ColormapRegistry::instance()
{
    static ColormapRegistry *registry = []()
    {
        auto *r = new ColormapRegistry();
        r->loadDirectory();
        return r;
    }();
    return *registry;
}

QStringList
ColormapRegistry::names() const
{
    QStringList list = kBuiltinNames;
    for (const CustomColormap &c : m_custom)
        list << c.name;
    return list;
}

QString
ColormapRegistry::name(int id) const
{
    if (id >= 0 && id < kBuiltinCount)
        return kBuiltinNames[id];
    const int i = id - kBuiltinCount;
    return (i >= 0 && i < m_custom.size()) ? m_custom[i].name : QString();
}

int
ColormapRegistry::indexOf(const QString &name) const
{
    const QStringList list = names();
    for (int i = 0; i < list.size(); ++i)
        if (list[i].compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

QByteArray
ColormapRegistry::lutData() const
{
    QByteArray data;
    data.reserve(m_custom.size() * kLutSize * 3);
    for (const CustomColormap &c : m_custom)
        for (QRgb px : c.lut)
        {
            data.append(char(qRed(px)));
            data.append(char(qGreen(px)));
            data.append(char(qBlue(px)));
        }
    return data;
}

QString
ColormapRegistry::directory() const
{
    const QString dir
        = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
          + "/colormaps";
    QDir().mkpath(dir);
    return dir;
}

void
ColormapRegistry::loadDirectory()
{
    const QDir dir(directory());
    const auto files = dir.entryInfoList({"*.csv", "*.txt", "*.json"},
                                         QDir::Files, QDir::Name);
    for (const QFileInfo &f : files)
    {
        CustomColormap cm;
        QString error;
        if (parseFile(f.absoluteFilePath(), cm, &error))
            insert(std::move(cm));
        else
            qWarning("Colormap %s: %s", qPrintable(f.fileName()),
                     qPrintable(error));
    }
    emit changed();
}

bool
ColormapRegistry::parseFile(const QString &path, CustomColormap &out,
                            QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = "cannot open " + path;
        return false;
    }
    const QByteArray data = file.readAll();

    QString name;
    QVector<Stop> stops;
    const bool isJson
        = QFileInfo(path).suffix().compare("json", Qt::CaseInsensitive) == 0;
    const bool ok = isJson ? parseJson(data, name, stops, error)
                           : parseCsv(QString::fromUtf8(data), stops, error);
    if (!ok)
        return false;
    if (stops.size() < 2)
    {
        if (error)
            *error = "needs at least two colors";
        return false;
    }

    out.name = name.trimmed().isEmpty() ? QFileInfo(path).completeBaseName()
                                        : name.trimmed();
    out.path = path;
    out.lut  = resample(stops);
    return true;
}

// Returns the id, or -1 when full. Replaces a custom colormap of that name.
int
ColormapRegistry::insert(CustomColormap cm)
{
    // A built-in name would be unreachable by name (e.g. --colormap)
    const int existing = indexOf(cm.name);
    if (existing >= 0 && existing < kBuiltinCount)
        cm.name += " (custom)";

    for (int i = 0; i < m_custom.size(); ++i)
        if (m_custom[i].name.compare(cm.name, Qt::CaseInsensitive) == 0)
        {
            m_custom[i] = std::move(cm);
            return kBuiltinCount + i;
        }
    if (m_custom.size() >= kMaxCustom)
        return -1;
    m_custom << std::move(cm);
    return kBuiltinCount + m_custom.size() - 1;
}

int
ColormapRegistry::addFile(const QString &path, bool copyToDir, QString *error)
{
    CustomColormap cm;
    if (!parseFile(path, cm, error))
        return -1;

    if (copyToDir)
    {
        const QString dest = directory() + "/" + QFileInfo(path).fileName();
        if (QFileInfo(dest) != QFileInfo(path))
        {
            QFile::remove(dest);
            if (QFile::copy(path, dest))
                cm.path = dest;
        }
    }

    const int id = insert(std::move(cm));
    if (id < 0)
    {
        if (error)
            *error = QString("at most %1 custom colormaps are supported")
                         .arg(kMaxCustom);
        return -1;
    }
    emit changed();
    return id;
}
