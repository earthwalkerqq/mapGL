#ifndef MAPGL_H
#define MAPGL_H

#include <cstdlib>
#include <QString>
#include <QDebug>

namespace mapGL {

const QString debugLogPrefix = QStringLiteral("[MAPGL_DEBUG]:");
const QString errorLogPrefix = QStringLiteral("[MAPGL_ERROR]:");

inline void debug(const QString msg = QString()) { qDebug() << debugLogPrefix << msg; }
inline void warning(const QString msg = QString()) { qDebug() << errorLogPrefix << msg; }

// test
// network
const QString k_server_url = QStringLiteral("http://172.20.10.2:8080");
const QString k_tile_req_postfix = QStringLiteral("/tiles");
// local fallback
const QString k_fallback_path =
    QStringLiteral("/Users/husen/Documents/test_map/materials/map/world_raster_z0_z6.mbtiles");

namespace math {

inline void parseKey(uint64_t key, uint8_t* z, uint32_t* x, uint32_t* y)
{
    *z = static_cast<uint8_t>(key >> 58);
    *x = static_cast<uint32_t>((key >> 29) & (((uint32_t)0x01 << 29) - 1));
    *y = static_cast<uint32_t>(key & (((uint32_t)0x01 << 29) - 1));
}

inline uint64_t makeKey(int z, uint32_t x, uint32_t y)
{
    uint64_t key = ((uint64_t)z << 58) | ((uint64_t)x << 29) | (uint64_t)y;
    return key;
}

} // namespace math

enum class TileQuality {
    Fallback = 0, // подложка
    Cached   = 1, // локальный кэш
    Network  = 2, // тайлы с севера
    Custom   = 10 // на будущее (векторные аэродромы и тд)
};

struct TileMeta {
    uint64_t key;
    QByteArray data;
    TileQuality quality;
};

} // namespace mapGL

#endif // MAPGL_H
