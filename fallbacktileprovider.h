#ifndef FALLBACKTILEPROVIDER_H
#define FALLBACKTILEPROVIDER_H

/*
 * Загрузка тайлов из подложки (z0-6)
 */

#include "tilestorage.h"
#include "mapGL.h"

#include <QString>

class FallbackTileProvider : public TileStorage
{
    Q_OBJECT
public:
    explicit FallbackTileProvider(const QString &trg, QObject *parent = nullptr)
        : TileStorage(trg, k_maxDefaultZoom, parent) {}

    mapGL::TileQuality quality(void) const override { return mapGL::TileQuality::Fallback; }

private:
    /*
     * Если в таблице mbtiles::metadata не будут
     * заполнены поля maxzoom, то отрисовка подложки
     * на больших зумах будет опираться на дефолт
     */
    static constexpr int k_maxDefaultZoom = 6;
};

#endif // FALLBACKTILEPROVIDER_H
