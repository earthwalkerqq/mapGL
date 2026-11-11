#include "fallbacktileprovider.h"

/*
 * Если в таблице mbtiles::metadata не будут
 * заполнены поля maxzoom, то отрисовка подложки
 * на больших зумах будет опираться на дефолт
 */
static const int k_maxDefaultZoom = 6;

static inline void prepareFallbackTileKey(const int maxzoom, uint8_t *z, uint32_t *x, uint32_t *y)
{
    if (static_cast<int>(*z) <= maxzoom)
        return;

    uint8_t div_zoom = *z - maxzoom;
    *z = static_cast<uint8_t>(maxzoom);
    *x >>= div_zoom;
    *y >>= div_zoom;
}

void FallbackTileProvider::load(void)
{
    if (m_err) {
        return;
    }

    const QVector<uint64_t> keys = getVisibleTiles();

    for (const uint64_t &key : keys)
    {
        if (m_set_miss.contains(key)) {
            emit failedTile(key, QStringLiteral("The tile was not found previously"));
            continue;
        }

        uint8_t z;
        uint32_t x, y;
        mapGL::math::parseKey(key, &z, &x, &y);

        if (m_fill_meta && (z < m_minzoom)) {
            emit failedTile(key, QStringLiteral("Fallback buffer doesnt have z%1").arg(z));
            return;
        }

        int maxzoom = (m_fill_meta) ? m_maxzoom : k_maxDefaultZoom;
        prepareFallbackTileKey(maxzoom, &z, &x, &y);

        QPair<Tiles, bool> r = m_db.from<Tiles>()
                                   .select(TilesFields::tile_data)
                                   .where(TilesFields::zoom_level, Comparison::Equal, z)
                                   .where(TilesFields::tile_column, Comparison::Equal, x)
                                   .where(TilesFields::tile_row, Comparison::Equal, y)
                                   .first();

        if (r.second) {
            QByteArray data = r.first.tile_data;
            emit readyTile({key, std::move(data), quality()});
        } else {
            m_set_miss.insert(key);
            emit failedTile(key, m_db.lastError());
        }
    }
}


void FallbackTileProvider::prepare()
{
    QPair<Metadata, bool> r = m_db.from<Metadata>()
        .select(MetadataFields::minzoom, MetadataFields::maxzoom)
        .first();
    if (r.second) {
        m_minzoom = r.first.minzoom.toInt();
        m_maxzoom = r.first.maxzoom.toInt();
        m_fill_meta = true;
    }
}
