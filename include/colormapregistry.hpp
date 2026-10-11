#pragma once

#include "colormap.hpp"

#include <QByteArray>
#include <QObject>
#include <QRgb>
#include <QString>
#include <QStringList>
#include <QVector>

// A user-defined colormap: 256 RGB entries read from a CSV or JSON file
struct CustomColormap
{
    QString name;
    QString path;
    QVector<QRgb> lut; // kLutSize entries
};

// All colormaps by id: the built-in ones (the Colormap enum) followed by the
// user's, which get ids kBuiltinCount, kBuiltinCount + 1, ...
//
// Custom colormaps are files in the colormaps directory (loaded at startup),
// or added at run time with addFile(). CSV: one "r,g,b" (or "#rrggbb") per
// line. JSON: {"name": "...", "colors": [...]}, each color a "#rrggbb"
// string, an [r, g, b] array or {"position": 0..1, "color": ...}.
class ColormapRegistry : public QObject
{
    Q_OBJECT

public:
    static constexpr int kBuiltinCount = int(Colormap::RAINBOW) + 1;
    static constexpr int kLutSize      = 256;
    static constexpr int kMaxCustom    = 64;

    static ColormapRegistry &instance();

    int count() const
    {
        return kBuiltinCount + m_custom.size();
    }
    int customCount() const
    {
        return m_custom.size();
    }
    const CustomColormap &custom(int i) const
    {
        return m_custom[i];
    }
    static bool isCustom(int id)
    {
        return id >= kBuiltinCount;
    }

    QStringList names() const;
    QString name(int id) const;
    // Case-insensitive; -1 when unknown
    int indexOf(const QString &name) const;

    // kLutSize * 3 bytes (RGB8) per custom colormap, row after row
    QByteArray lutData() const;

    // Directory scanned at startup (created on demand)
    QString directory() const;
    void loadDirectory();

    // Parses `path` and registers it, replacing a custom colormap of the same
    // name. With copyToDir the file is also copied into directory() so it is
    // there next time. Returns the new id, or -1 with `error` set.
    int addFile(const QString &path, bool copyToDir, QString *error = nullptr);

    static bool parseFile(const QString &path, CustomColormap &out,
                          QString *error);

signals:
    void changed();

private:
    ColormapRegistry();
    int insert(CustomColormap cm);

    QVector<CustomColormap> m_custom;
};
