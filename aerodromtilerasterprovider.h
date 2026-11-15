#ifndef AERODROMTILERASTERPROVIDER_H
#define AERODROMTILERASTERPROVIDER_H

#include "tilestorage.h"
#include "mapGL.h"

#include <QString>

class AerodromTileRasterProvider : public TileStorage
{
    Q_OBJECT
public:
    AerodromTileRasterProvider(const QString &icao, const QString &trg, QObject *parent = nullptr);

    mapGL::TileQuality quality(void) const override { return mapGL::TileQuality::Aerodrom; }
    QString icao(void) const { return m_icao; }

private:
    QString m_icao;
};

#endif // AERODROMTILERASTERPROVIDER_H
