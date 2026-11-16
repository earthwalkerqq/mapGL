#include "aerodromtilevectorprovider.h"
#include "tileformat.h"
#include "vectortile.h"
#include "orm.h"

#include <QtMath>
#include <cmath>
#include <limits>

static constexpr double k_earthCircumferenceM = 40075016.686;

static QPointF toWorld(double latDeg, double lonDeg)
{
    const double phi = qDegreesToRadians(latDeg);
    return { (lonDeg + 180.0) / 360.0,
             (1.0 - std::log(std::tan(M_PI / 4 + phi / 2)) / M_PI) / 2.0 };
}

void AerodromTileVectorProvider::prepare(void)
{
    readMetadata();

    if (!mapGL::format::isVector(m_format)) {
        setError(QStringLiteral("%1: аэродромы (%2) поддерживаются только в векторном формате")
            .arg(m_target, QString::fromLatin1(m_format)));
        return;
    }

    readZones();
}

bool AerodromTileVectorProvider::readZones(void)
{
    DataBase db(mapGL::aeroSourceFile());
    if (!db.connect()) {
        setError(db.lastError());
        return false;
    }

    auto filter = std::make_shared<mapGL::VectorFilter>();

    for (const QString &icao : std::as_const(m_icaos)) {
        QPair<Aerodrom, bool> aero = db.from<Aerodrom>()
            .select(AerodromFields::long_rad, AerodromFields::lat_rad)
            .where(AerodromFields::kod_icao, Comparison::Equal, icao)
            .first();

        if (!aero.second) {
            mapGL::warning(QStringLiteral("аэродром %1 не найден в %2").arg(icao, mapGL::aeroSourceFile()));
            continue;
        }

        const double lat = qRadiansToDegrees(aero.first.lat_rad);
        const double lon = qRadiansToDegrees(aero.first.long_rad);

        const QList<Ranway> runways = db.from<Ranway>()
            .select(RanwayFields::rw_length)
            .where(RanwayFields::kod_icao, Comparison::Equal, icao)
            .exec();

        double maxRunwayM = 0.;
        for (const Ranway &rw : runways) {
            maxRunwayM = qMax(maxRunwayM, double(rw.rw_length));
        }
        if (maxRunwayM <= 0.) {
            maxRunwayM = k_defaultRunwayLengthM;
        }

        const QPointF center = toWorld(lat, lon);
        const double metersPerWorld = k_earthCircumferenceM * std::cos(qDegreesToRadians(lat));
        const double searchRadius = qMax(k_searchRadiusM, 2. * maxRunwayM) / metersPerWorld;

        QRectF area = findAerodromArea(icao, center, searchRadius);
        if (area.isNull()) {
            const double r = (maxRunwayM / 2. + k_fallbackMarginM) / metersPerWorld;
            area = QRectF(center.x() - r, center.y() - r, 2. * r, 2. * r);
            mapGL::debug(QStringLiteral("аэродром %1: полигона нет в данных, зона по ВПП").arg(icao));
        } else {
            const double m = k_areaMarginM / metersPerWorld;
            area.adjust(-m, -m, m, m);
        }

        filter->zones.push_back({icao, area});
    }

    if (filter->zones.isEmpty()) {
        setError(QStringLiteral("ни один аэродром из [%1] не найден").arg(m_icaos.join(QStringLiteral(", "))));
        return false;
    }

    m_filter = std::move(filter);
    return true;
}

QRectF AerodromTileVectorProvider::findAerodromArea(const QString &icao, const QPointF &center,
                                                    double searchRadius)
{
    const qint64 n = qint64(1) << k_searchZoom;
    const qint64 x0 = qint64(std::floor((center.x() - searchRadius) * n));
    const qint64 x1 = qint64(std::floor((center.x() + searchRadius) * n));
    const qint64 y0 = qMax<qint64>(0, qint64(std::floor((center.y() - searchRadius) * n)));
    const qint64 y1 = qMin<qint64>(n - 1, qint64(std::floor((center.y() + searchRadius) * n)));

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();

    for (qint64 tx = x0; tx <= x1; ++tx) {
        const uint32_t wrappedX = uint32_t(((tx % n) + n) % n);
        for (qint64 ty = y0; ty <= y1; ++ty) {
            QByteArray data;
            if (!readTile(mapGL::math::makeKey(k_searchZoom, wrappedX, uint32_t(ty)), &data)) {
                continue;
            }

            QVector<mapGL::mvt::Layer> layers;
            if (!mapGL::mvt::parse(mapGL::mvt::gunzip(data), &layers)) {
                continue;
            }

            for (const mapGL::mvt::Layer &layer : std::as_const(layers)) {
                if (layer.name != QLatin1String("aerodromes")) {
                    continue;
                }
                for (const mapGL::mvt::Feature &feature : layer.features) {
                    const QString code = feature.properties.value(QStringLiteral("icao"))
                                             .toString().trimmed().toUpper();
                    if (code != icao) {
                        continue;
                    }
                    for (const QPolygonF &part : feature.parts) {
                        for (const QPointF &p : part) {
                            const double wx = (double(tx) + p.x() / layer.extent) / double(n);
                            const double wy = (double(ty) + p.y() / layer.extent) / double(n);
                            minX = qMin(minX, wx);
                            minY = qMin(minY, wy);
                            maxX = qMax(maxX, wx);
                            maxY = qMax(maxY, wy);
                        }
                    }
                }
            }
        }
    }

    if (minX > maxX || minY > maxY) {
        return QRectF();
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

bool AerodromTileVectorProvider::inZones(uint64_t key) const
{
    uint8_t z;
    uint32_t x, y;
    mapGL::math::parseKey(key, &z, &x, &y);

    const qint64 n = qint64(1) << z;

    for (const mapGL::AerodromZone &zone : m_filter->zones) {
        const qint64 x0 = qint64(std::floor(zone.area.left() * n));
        const qint64 x1 = qint64(std::floor(zone.area.right() * n));
        const qint64 y0 = qMax<qint64>(0, qint64(std::floor(zone.area.top() * n)));
        const qint64 y1 = qMin<qint64>(n - 1, qint64(std::floor(zone.area.bottom() * n)));

        if (qint64(y) < y0 || qint64(y) > y1) {
            continue;
        }
        for (const qint64 tx : {qint64(x), qint64(x) + n, qint64(x) - n}) {
            if (tx >= x0 && tx <= x1) {
                return true;
            }
        }
    }
    return false;
}

void AerodromTileVectorProvider::load(const QVector<uint64_t> &keys, quint64 generation)
{
    if (m_err || !m_filter || isStale(generation)) {
        return;
    }

    for (const uint64_t key : storageKeys(keys)) {
        if (isStale(generation)) {
            return;
        }
        if (!inZones(key) || m_set_miss.contains(key) || alreadyHaveVector(key)) {
            continue;
        }

        QByteArray data;
        if (readTile(key, &data)) {
            emit readyTileVector(mapGL::TileMeta{key, mapGL::RawTile{data, quality(), m_format, m_filter}});
        }
    }
}
