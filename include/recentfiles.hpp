#pragma once

#include <QString>
#include <QStringList>

// Most-recently-opened files, kept in a plain text file (one path per line)
// in the application data directory, e.g. ~/.local/share/Hpixview/ on Linux.
class RecentFiles
{
public:
    explicit RecentFiles(int maxCount = 10);

    // Newest first; files that no longer exist are dropped
    QStringList files() const;

    void add(const QString &path);
    void remove(const QString &path);
    void clear();

    // Where the list is stored
    static QString storagePath();

private:
    void load();
    void save() const;

    int m_maxCount;
    QStringList m_files;
};
