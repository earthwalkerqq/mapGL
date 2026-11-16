#ifndef FALLBACKTILEPROVIDER_H
#define FALLBACKTILEPROVIDER_H

/*
 * Загрузка тайлов из подложки (z0-6)
 */

#include "tilestorage.h"
#include "mapGL.h"
#include "standartsourcepath.h"

#include <QString>

class FallbackTileProvider : public TileStorage
{
    Q_OBJECT
public:
    explicit FallbackTileProvider(QObject *parent = nullptr)
        : TileStorage(mapGL::fallbackTileFile(), k_maxDefaultZoom, parent) {}

    mapGL::TileQuality quality(void) const override { return mapGL::TileQuality::Fallback; }

    void load(const QVector<uint64_t> &keys, quint64 generation) override;

private:
    void prepare(void) override;

    /*
     * Если в таблице mbtiles::metadata не будут
     * заполнены поля maxzoom, то отрисовка подложки
     * на больших зумах (>6) будет опираться на дефолт
     */
    static constexpr int k_maxDefaultZoom = 6;
};

#endif // FALLBACKTILEPROVIDER_H
