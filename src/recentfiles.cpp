#include "recentfiles.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

RecentFiles::RecentFiles(int maxCount) : m_maxCount(maxCount)
{
    load();
}

QString
RecentFiles::storagePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/recent_files.txt";
}

QStringList
RecentFiles::files() const
{
    QStringList existing;
    for (const QString &f : m_files)
        if (QFileInfo::exists(f))
            existing << f;
    return existing;
}

void
RecentFiles::add(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    m_files.removeAll(abs);
    m_files.prepend(abs);
    while (m_files.size() > m_maxCount)
        m_files.removeLast();
    save();
}

void
RecentFiles::remove(const QString &path)
{
    if (m_files.removeAll(QFileInfo(path).absoluteFilePath()) > 0)
        save();
}

void
RecentFiles::clear()
{
    m_files.clear();
    save();
}

void
RecentFiles::load()
{
    m_files.clear();
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return; // nothing saved yet

    QTextStream in(&file);
    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();
        if (!line.isEmpty() && !m_files.contains(line))
            m_files << line;
        if (m_files.size() >= m_maxCount)
            break;
    }
}

void
RecentFiles::save() const
{
    QDir().mkpath(QFileInfo(storagePath()).absolutePath());

    // QSaveFile writes to a temporary file and renames it, so a crash can't
    // leave a half-written list
    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream out(&file);
    for (const QString &f : m_files)
        out << f << '\n';
    out.flush();
    file.commit();
}
