#include "tilestorage.h"
#include "tileformat.h"

#include <QFileInfo>
#include <QStringList>
#include <QtMath>
#include <cmath>

static inline void prepareStorageTileKey(const int maxzoom, uint8_t *z, uint32_t *x, uint32_t *y)
{
    if (static_cast<int>(*z) <= maxzoom)
        return;

    uint8_t div_zoom = *z - maxzoom;
    *z = static_cast<uint8_t>(maxzoom);
    *x >>= div_zoom;
    *y >>= div_zoom;
}

static inline double tileLat(double y, double n)
{
    return qRadiansToDegrees(std::atan(std::sinh(M_PI * (1.0 - 2.0 * y / n))));
}

void TileStorage::init(void)
{
    if (m_target.isEmpty() || !QFileInfo::exists(m_target)) {
        setError(QStringLiteral("файл карты не найден: %1").arg(m_target));
        return;
    }
    if (!m_db.connect()) {
        setError(m_db.lastError());
        return;
    }
    prepare();
}

void TileStorage::setError(const QString &msg)
{
    m_err = true;
    m_lastError = msg;
    mapGL::warning(m_lastError);
}

void TileStorage::readMetadata(void)
{
    const QList<Metadata> rows = m_db.from<Metadata>()
        .select(MetadataFields::name, MetadataFields::value)
        .exec();

    for (const Metadata &row : rows) {
        bool ok = false;
        if (row.name == QLatin1String("minzoom")) {
            const int v = row.value.toInt(&ok);
            if (ok) m_minzoom = v;
        } else if (row.name == QLatin1String("maxzoom")) {
            const int v = row.value.toInt(&ok);
            if (ok) m_maxzoom = v;
        } else if (row.name == QLatin1String("format")) {
            m_format = mapGL::format::normalize(row.value.toLatin1());
        } else if (row.name == QLatin1String("bounds")) {
            const QStringList parts = row.value.split(QLatin1Char(','));
            if (parts.size() == 4) {
                double v[4];
                bool all = true;
                for (int i = 0; i < 4 && all; ++i) {
                    v[i] = parts[i].trimmed().toDouble(&all);
                }
                if (all && v[0] < v[2] && v[1] < v[3]) {
                    m_minLon = v[0];
                    m_minLat = v[1];
                    m_maxLon = v[2];
                    m_maxLat = v[3];
                    m_hasBounds = true;
                }
            }
        }
    }
}

QVector<uint64_t> TileStorage::storageKeys(const QVector<uint64_t> &keys) const
{
    const int maxzoom = (m_maxzoom >= 0) ? m_maxzoom : m_defaultMaxZoom;

    QVector<uint64_t> result;
    // товарищ, скажи "нет" повторам!
    QSet<uint64_t> seen;
    for (const uint64_t key : keys)
    {
        uint8_t z;
        uint32_t x, y;
        mapGL::math::parseKey(key, &z, &x, &y);

        if (m_minzoom >= 0 && z < m_minzoom) {
            continue;
        }

        prepareStorageTileKey(maxzoom, &z, &x, &y);
        if (!inBounds(z, x, y)) {
            continue;
        }

        const uint64_t storageKey = mapGL::math::makeKey(z, x, y);
        if (seen.contains(storageKey)) {
            continue;
        }
        seen.insert(storageKey);
        result.push_back(storageKey);
    }
    return result;
}

bool TileStorage::readTile(uint64_t key, QByteArray *data)
{
    uint8_t z;
    uint32_t x, y;
    mapGL::math::parseKey(key, &z, &x, &y);

    // MBTiles хранит строки в схеме TMS, камера считает в XYZ
    const uint32_t tmsY = (1u << z) - 1u - y;

    QPair<Tiles, bool> r = m_db.from<Tiles>()
                               .select(TilesFields::tile_data)
                               .where(TilesFields::zoom_level, Comparison::Equal, static_cast<int>(z))
                               .where(TilesFields::tile_column, Comparison::Equal, static_cast<int>(x))
                               .where(TilesFields::tile_row, Comparison::Equal, static_cast<int>(tmsY))
                               .first();
    if (!r.second) {
        m_set_miss.insert(key);
        return false;
    }
    *data = r.first.tile_data;
    return true;
}

bool TileStorage::inBounds(uint8_t z, uint32_t x, uint32_t y) const
{
    if (!m_hasBounds) {
        return true;
    }

    const double n = static_cast<double>(1u << z);
    const double west = x / n * 360.0 - 180.0;
    const double east = (x + 1) / n * 360.0 - 180.0;
    const double north = tileLat(y, n);
    const double south = tileLat(y + 1, n);

    return east > m_minLon && west < m_maxLon && north > m_minLat && south < m_maxLat;
}
