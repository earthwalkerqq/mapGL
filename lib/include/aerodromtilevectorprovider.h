#ifndef AERODROMTILEVECTORPROVIDER_H
#define AERODROMTILEVECTORPROVIDER_H

#include "tilestorage.h"
#include "standartsourcepath.h"

#include <QRectF>
#include <QStringList>
#include <memory>

class AerodromTileVectorProvider : public TileStorage
{
    Q_OBJECT
public:
    explicit AerodromTileVectorProvider(const QStringList &icaos, QObject *parent = nullptr)
        : TileStorage(mapGL::aeroVector(), mapGL::k_maxTileVectorZoom, parent), m_icaos(icaos) {}

    mapGL::TileQuality quality(void) const override { return mapGL::TileQuality::AerodromVector; }
    bool isVector(void) const override { return true; }

    QStringList icaos(void) const { return m_icaos; }

    void load(const QVector<uint64_t> &keys, quint64 generation) override;

private:
    void prepare(void) override;
    bool readZones(void);
    QRectF findAerodromArea(const QString &icao, const QPointF &center, double searchRadius);
    bool inZones(uint64_t key) const;

    static constexpr int k_searchZoom = 12;
    static constexpr double k_searchRadiusM = 10000.;
    static constexpr double k_areaMarginM = 500.;
    static constexpr double k_defaultRunwayLengthM = 3000.;
    static constexpr double k_fallbackMarginM = 2000.;

    QStringList m_icaos;
    std::shared_ptr<const mapGL::VectorFilter> m_filter;
};

#endif // AERODROMTILEVECTORPROVIDER_H
