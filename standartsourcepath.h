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
    const QString base = Aurora::Application::pathTo(relative).toLocalFile();
#elif defined(MAPGL_DATA_DIR)
    const QString base = QStringLiteral(MAPGL_DATA_DIR);
#else
    const QString base = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("../share/%1")
        .arg(QFileInfo(QCoreApplication::applicationFilePath()).fileName()));
#endif

    const QString dir = QDir(base).filePath(sub);
    if (!QDir().mkpath(dir)) {
        return QString();
    }
    return dir;
}

inline QString aeroSourceDir() { return writableDir(QStringLiteral("data/aerodrom.sqlite")); }
inline QString fallbackTileFile() { return localDir(QStringLiteral("data/map/world_raster_z0_z6.mbtiles")); }
inline QString aeroDB(const QString icao) { return writableDir(QStringLiteral("aerodroms/%1.mbtiles").arg(icao)); }
inline QString aeroVector() { return localDir(QStringLiteral("data/aeroports_map")); }

} // namespace mapGL

#endif // STANDARTSOURCEPATH_H
