#include "tileloader.h"
#include "mapGL.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QFileInfo>
#include <QDir>
#include <QtMath>
#include <QTimer>

static inline QString getFetchUrlTarget(float &lat, float &lon, QString &icao)
{
    QString postfix = QStringLiteral("%1?lat=%2&lon=%3&kod_icao=%4")
        .arg(mapGL::server::k_tile_fetch_postfix).arg(lat).arg(lon).arg(icao);

    return postfix;
}

TileLoader::TileLoader(const QString &icao, QObject *parent)
    : m_icao(icao), m_target(mapGL::aeroDB(m_icao)), QObject{parent},
    m_db(m_target)) {}

TileLoader::~TileLoader(void)
{
    const QList<QNetworkReply *> replies = m_inFlight.values();
    m_inFlight.clear();
    for (QNetworkReply *reply : replies) {
        reply->disconnect(this);
        reply->abort();
    }
}

bool TileLoader::prepare(void)
{
    {
        DataBase aero_db(mapGL::aeroSourceDir());
        if (!aero_db.connect()) {
            m_lastError = aero_db.lastError();
            mapGL::warning(m_lastError);
            m_err = true;
            return false;
        }

        QPair<Aerodrom, bool> r = aero_db.from<Aerodrom>().select()
                                      .where(AerodromFields::kod_icao, Comparison::Equal, m_icao).first();
        if (r.second) {
            m_lon = qRadiansToDegrees(r.first.long_rad);
            m_lat = qRadiansToDegrees(r.first.lat_rad);
        } else {
            m_lastError = QStringLiteral("Aerodrom not found");
            mapGL::warning(m_lastError);
            m_err = true;
            return false;
        }
    }

    uint8_t err = 0x00;
    err |= static_cast<uint8_t>(!m_db.createDBStruct<Metadata>());
    err |= static_cast<uint8_t>(!m_db.createDBStruct<Tiles>()) << 1;
    if (err & 0x01) {
        m_lastError = QStringLiteral("Error from Metadata table create: %1")
            .arg(m_db.lastError());
        mapGL::warning(m_lastError);
        m_err = true;
        return false;
    } else if (err & (0x01 << 1)) {
        m_lastError = QStringLiteral("Error from Tiles table create: %1")
            .arg(m_db.lastError());
        mapGL::warning(m_lastError);
        m_err = true;
        return false;
    }
    return true;

}

void TileLoader::init(void)
{
    if (m_target.isEmpty() || !m_db.connect()) {
        m_err = true;
        m_lastError = m_db.lastError();
        mapGL::warning(m_lastError);
        return;
    }
    prepare();
}

void TileLoader::load(void)
{
    /*
     * запрашиваем с сервера необходимую информацию
     * о тайлах (сервер по icao сам расчитывает какие
     * тайлы на район аэродрома необходимо скачать)
     * Далее делаем обычный запрос на получение и
     * сохраняем тайлы на тачке
     */
    fetchAirportTiles();
}

void TileLoader::fetchAirportTiles(void)
{
    QUrl requestUrl = mapGL::server::k_serv_target.resolved(QUrl(getFetchUrlTarget(m_lat, m_lon, m_icao)));

    mapGL::debugLog(QStringLiteral("FETCH %1").arg(requestUrl.toString()));

    QNetworkRequest request(requestUrl);
    request.setTransferTimeout(k_transfer_timeout_ms);

    QNetworkReply *reply = m_manager->get(QNetworkRequest(requestUrl));
    connect(reply, &QNetworkReply::finished, this, [reply, dwnlSrc, this]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            // нет связи — повторить через 1, 2, 4, 8, 16 с
            if (status == 0 && m_fetchAttempt < kMaxAttempts) {
                const int delayMs = 1000 << m_fetchAttempt;
                ++m_fetchAttempt;
                QTimer::singleShot(delayMs, this, &AerodromLoader::fetchAirportTiles);
                return;
            }
            emit aerodromTilesFailed(status, QStringLiteral("HTTP %1: %2")
                                                 .arg(status)
                                                 .arg(reply->errorString()));
            return;
        }
        m_fetchAttempt = 0;

        QByteArray body = reply->readAll().trimmed();

        QJsonParseError err;

        const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
        if (err.error != QJsonParseError::NoError) {
            emit aerodromTilesFailed(status, QStringLiteral("pyramid: битый JSON: %1")
                                                 .arg(err.errorString()));
            return;
        }

        if (!doc.isObject()) {
            emit aerodromTilesFailed(status, QStringLiteral("pyramid: ждали объект"));
            return;
        }

        parseJsonFetchResponse(doc);
        insertTileMeta();

        mapGL::debugLog(QStringLiteral("pyramid: тайлов в списке %1").arg(m_aero_meta.keys.size()));
        load_tiles();
    });
}

void TileLoader::insertTileMeta(void)
{
    /*
     * TODO: дописать запись мета информации
     * тайлов в mbtiles::metadata
     * и записывать сырые тайлы в бд
     */
}

void TileLoader::processQueue(void)
{
    while (m_inFlight.size() < k_max_requested_tile && !m_tileLoadKeys.isEmpty()) {
        const uint64_t key = m_queue.takeFirst();
        if (m_inFlight.contains(key) || m_missing.contains(key)) {
            continue;
        }
        sendReqForGetTile(key);
    }
}

void TileLoader::parseJsonFetchResponse(const QJsonDocument &doc)
{
    const QJsonObject rootObj = doc.object();

    m_mapVersion = rootObj.value(QStringLiteral("map_version")).toString();
    m_minzoom = rootObj.value(QStringLiteral("minzoom")).toInt();
    m_maxzoom = rootObj.value(QStringLiteral("maxzoom")).toInt();

    const QJsonArray layersArray = rootObj.value(QStringLiteral("tiles")).toArray();

    for (const QJsonValue &layerValue : layersArray) {
        const QJsonObject layerObj = layerValue.toObject();
        const int z = layerObj.value(QStringLiteral("z")).toInt(-1);
        if (z < 0 || z > mapGL::k_maxTileZoom)
            continue;
        const uint32_t n = 1u << z;   // тайлов по стороне на уровне z

        for (const QJsonValue &tileValue : layerObj.value(QStringLiteral("tiles")).toArray()) {
            const QJsonArray pair = tileValue.toArray();
            if (pair.size() != 2)
                continue;
            const int x = pair[0].toInt(-1);
            const int y = pair[1].toInt(-1);
            if (x < 0 || y < 0 || uint32_t(x) >= n || uint32_t(y) >= n)
                continue;

            m_tileLoadKeys.insert(mapGL::math::makeKey(z, uint32_t(x), uint32_t(y)));
        }
    }
}
