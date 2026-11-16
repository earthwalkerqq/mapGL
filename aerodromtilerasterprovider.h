#ifndef AERODROMTILERASTERPROVIDER_H
#define AERODROMTILERASTERPROVIDER_H

/*
 * класс для отрисовки растовых тайлов аэродромов
 * скаченных из сети
 */

#include "tilestorage.h"
#include "mapGL.h"

#include <QString>

class AerodromTileRasterProvider : public TileStorage
{
    Q_OBJECT
public:
    explicit AerodromTileRasterProvider(const QString &icao, QObject *parent = nullptr);

    mapGL::TileQuality quality(void) const override { return mapGL::TileQuality::Aerodrom; }
    QString icao(void) const { return m_icao; }

    void load(const QVector<uint64_t> &keys, quint64 generation) override;

private:
    void prepare(void) override;

    QString m_icao;
};

#endif // AERODROMTILERASTERPROVIDER_H
