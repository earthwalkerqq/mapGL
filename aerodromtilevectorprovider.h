#ifndef AERODROMTILEVECTORPROVIDER_H
#define AERODROMTILEVECTORPROVIDER_H

#include "tilestorage.h"

class AerodromTileVectorProvider : public TileStorage
{
    Q_OBJECT
public:
    explicit AerodromTileVectorProvider(QObject *parent = nullptr);

private:
    void prepare(void) override;
};

#endif // AERODROMTILEVECTORPROVIDER_H
