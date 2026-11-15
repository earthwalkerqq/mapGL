#include "aerodromtilerasterprovider.h"

AerodromTileRasterProvider::AerodromTileRasterProvider(const QString &icao, const QString &trg, QObject *parent)
    : TileStorage(trg, mapGL::k_maxTileZoom, parent), m_icao(icao) {}
