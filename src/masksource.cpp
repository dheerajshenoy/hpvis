#include "masksource.hpp"

#include <QFileInfo>

QString
MaskSource::key() const
{
    if (map)
        return "memory:" + label;
    const QString abs = QFileInfo(path).absoluteFilePath();
    // A plain file keeps its path as the key
    return (hdu > 0 || column > 0)
               ? QString("%1#%2:%3").arg(abs).arg(hdu).arg(column)
               : abs;
}
