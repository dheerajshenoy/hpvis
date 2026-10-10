#include "shaderloader.hpp"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <iostream>

static bool
expand(const QString &path, QSet<QString> &seen, QString &out)
{
    if (seen.contains(path))
        return true;
    seen.insert(path);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        std::cerr << "[Shader error] Cannot open " << path.toStdString()
                  << std::endl;
        return false;
    }

    static const QRegularExpression includeRe(
        R"re(^\s*#include\s+"([^"]+)"\s*$)re");
    const QString dir = QFileInfo(path).path();

    const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
    for (const QString &line : lines)
    {
        const auto m = includeRe.match(line);
        if (m.hasMatch())
        {
            if (!expand(dir + "/" + m.captured(1), seen, out))
                return false;
        }
        else
            out += line + "\n";
    }
    return true;
}

QString
loadShaderSource(const QString &resourcePath)
{
    QSet<QString> seen;
    QString out;
    if (!expand(resourcePath, seen, out))
        return {};
    return out;
}
