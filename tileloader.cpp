#include "tileloader.h"
#include "mapGL.h"

#include <QJsonArray>
#include <QJsonValue>

static inline QString getReqUrlTarget(int z, int x, int y)
{
    QString postfix = QStringLiteral("%1/%2/%3/%4")
        .arg(mapGL::k_tile_req_postfix).arg(z).arg(x).arg(y);

    return postfix;
}

static inline QString getFetchUrlTarget(float &lat, float &lon, QString &icao)
{
    QString postfix = QStringLiteral("%1?lat=%2&lon=%3&kod_icao=%4")
    .arg(mapGL::k_tile_fetch_postfix).arg(lat).arg(lon).arg(icao);

    return postfix;
}

// TODO: доделать loader аэродромов

TileLoader::TileLoader(QObject *parent)
    : QObject{parent},
    TileServerMeta(new QNetworkAccessManager(this)) {}

void TileLoader::fetchAirportTiles(void)
{
    QUrl requestUrl = m_server.resolved(QUrl(QStringLiteral("/tiles/pyramid")));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("lat"), QString::number(m_lat, 'f', 6));
    q.addQueryItem(QStringLiteral("lon"), QString::number(m_lon, 'f', 6));
    q.addQueryItem(QStringLiteral("kod_icao"), m_aero_meta.icao);
    requestUrl.setQuery(q);

    mapGL::debugLog(QStringLiteral("FETCH %1").arg(requestUrl.toString()));

    QNetworkReply *reply = m_manager.get(QNetworkRequest(requestUrl));
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

        parseJsonFetchResponse(doc, dwnlSrc);

        mapGL::debugLog(QStringLiteral("pyramid: тайлов в списке %1").arg(m_aero_meta.keys.size()));
        load_tiles();
    });
}

void TileLoader::parseJsonFetchResponse(const QJsonDocument &doc, mapGL::DownloadSource dwnlSrc)
{
    const QJsonObject rootObj = doc.object();

    m_aero_meta.version = rootObj.value(QStringLiteral("map_version")).toString();
    m_aero_meta.icao = rootObj.value(QStringLiteral("kod_icao")).toString();
    m_aero_meta.minzoom = rootObj.value(QStringLiteral("minzoom")).toInt();
    m_aero_meta.maxzoom = rootObj.value(QStringLiteral("maxzoom")).toInt();

    const QJsonArray layersArray = rootObj.value(QStringLiteral("tiles")).toArray();

    for (const QJsonValue &layerValue : layersArray) {
        const QJsonObject layerObj = layerValue.toObject();
        const int z = layerObj.value(QStringLiteral("z")).toInt(-1);
        if (z < 0 || z > kMaxTileZoom)
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

            m_aero_meta.keys.push_back(TileCore::makeKey(z, uint32_t(x), uint32_t(y)));
        }
    }

    QSqlQuery queryAero(m_connAero.db());

}
