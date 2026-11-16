#ifndef STANDARTSOURCEPATH_H
#define STANDARTSOURCEPATH_H

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

/*
 * при деплои define MAPGL_AURORA.
 * при локальном тестировании define MAPGL_SOURCE_DIR <путь_к_директории>
 * и define MAPGL_DATA_DIR <путь_к_директории>
 */

#ifdef MAPGL_AURORA
#include <auroraapp.h>
#endif

namespace mapGL {

/*
 * директория с правами на запись
 * для скаченных тайлов аэродромов
 * и тд
 */
inline QString writableDir(const QString &sub)
{
#ifdef MAPGL_SOURCE_DIR
    const QString base = QStringLiteral(MAPGL_SOURCE_DIR);
#else
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
    const QString dir = QDir(base).filePath(sub);
    if (!QDir().mkpath(dir)) {
        return QString();
    }
    return dir;
}

/*
 * директория с правами readonly
 * c подложкой/аэродромами и всеми другими
 * rpm данными
 */
inline QString localDir(const QString &sub)
{
#ifdef MAPGL_AURORA
    const QString base = Aurora::Application::pathTo(sub).toLocalFile();
    return base;
#else
#if defined(MAPGL_DATA_DIR)
    const QString base = QStringLiteral(MAPGL_DATA_DIR);
#else
    const QString base = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("../share/%1")
        .arg(QFileInfo(QCoreApplication::applicationFilePath()).fileName()));
#endif
    return QDir(base).filePath(sub);
#endif
}

inline QString writableFile(const QString &dirSub, const QString &fileName)
{
    const QString dir = writableDir(dirSub);
    return dir.isEmpty() ? QString() : QDir(dir).filePath(fileName);
}

inline QString aeroSourceFile() { return localDir(QStringLiteral("data/Aerodrom_obrazec.sqlite")); }
inline QString fallbackTileFile() { return localDir(QStringLiteral("map/world_raster_z0_z6.mbtiles")); }
inline QString aeroDB(const QString &icao)
{
    return writableFile(QStringLiteral("aerodroms"), QStringLiteral("%1.mbtiles").arg(icao));
}
inline QString aeroVector() { return localDir(QStringLiteral("aeroports_map/airports_world_z8_z16.mbtiles")); }

} // namespace mapGL

#endif // STANDARTSOURCEPATH_H
