#include "fallbacktileprovider.h"
#include "tileformat.h"

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

void FallbackTileProvider::init(void)
{
    if (!m_db.connect()) {
        m_err = true;
        m_lastError = m_db.lastError();
        mapGL::warning(m_lastError);
        return;
    }
    prepare();
}

void FallbackTileProvider::load(const QVector<uint64_t> &keys, quint64 generation)
{
    if (m_err || isStale(generation)) {
        return;
    }

    const int maxzoom = (m_maxzoom >= 0) ? m_maxzoom : k_maxDefaultZoom;

    /*
     * Видимые ключи -> ключи подложки (предки на maxzoom), без повторов.
     * Порядок сохраняется: ключи пришли отсортированными по приоритету.
     * На z12 сотни видимых тайлов обычно сводятся к 1-4 тайлам z6.
     */
    QVector<uint64_t> fallbackKeys;
    for (const uint64_t key : keys)
    {
        uint8_t z;
        uint32_t x, y;
        mapGL::math::parseKey(key, &z, &x, &y);

        if (m_minzoom >= 0 && z < m_minzoom) {
            continue;
        }

        prepareFallbackTileKey(maxzoom, &z, &x, &y);
        const uint64_t fbKey = mapGL::math::makeKey(z, x, y);
        fallbackKeys.push_back(fbKey);
    }

    for (const uint64_t key : std::as_const(fallbackKeys)) {
        if (isStale(generation)) {
            return;
        }
        if (m_set_miss.contains(key) || alreadyHave(key)) {
            continue;
        }

        uint8_t z;
        uint32_t x, y;
        mapGL::math::parseKey(key, &z, &x, &y);

        // MBTiles хранит строки в схеме TMS (0 — юг), камера считает в XYZ (0 — север)
        const uint32_t tmsY = (1u << z) - 1u - y;

        QPair<Tiles, bool> r = m_db.from<Tiles>()
            .select(TilesFields::tile_data)
            .where(TilesFields::zoom_level, Comparison::Equal, static_cast<int>(z))
            .where(TilesFields::tile_column, Comparison::Equal, static_cast<int>(x))
            .where(TilesFields::tile_row, Comparison::Equal, static_cast<int>(tmsY))
            .first();

        if (r.second) {
            emit readyTile(mapGL::TileMeta{key, mapGL::RawTile{r.first.tile_data, quality(), m_format}});
        } else {
            m_set_miss.insert(key);
            emit failedTile(key, QStringLiteral("нет в подложке"));
        }
    }
}

void FallbackTileProvider::prepare()
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
        }
    }

    /*
     * в данной версии ПО карта подложка имеет растовый
     * формат и загрузка векторов не допустима,
     * иначе упадет позже, на этапе рендеринга
     */
    if (mapGL::format::isVector(m_format)) {
        m_err = true;
        m_lastError = QStringLiteral("%1: векторные тайлы (%2) этим провайдером не поддерживаются")
                          .arg(m_target, QString::fromLatin1(m_format));
    } else if (!m_format.isEmpty() && !mapGL::format::isSupported(m_format)) {
        m_err = true;
        m_lastError = QStringLiteral("%1: нет плагина Qt для формата %2")
                          .arg(m_target, QString::fromLatin1(m_format));
    }
    if (m_err) {
        mapGL::warning(m_lastError);
    }
}
