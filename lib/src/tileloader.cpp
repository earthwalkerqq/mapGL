#include "tileloader.h"
#include "mapGL.h"
#include "standartsourcepath.h"
#include "tileformat.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSqlError>
#include <QTimer>
#include <QUrlQuery>
#include <QtMath>
#include <algorithm>
#include <limits>

static constexpr int k_commitBatch = 256;

TileLoader::TileLoader(const QString &icao, QObject *parent)
    : QObject{parent}
    , NetworkService(this)
    , m_icao(icao)
    , m_target(mapGL::aeroDB(icao))
    , m_partTarget(m_target + QStringLiteral(".part"))
    , m_db(m_partTarget) {}

TileLoader::~TileLoader(void)
{
    cancelAll();
    if (m_fetchReply) {
        m_fetchReply->disconnect(this);
        m_fetchReply->abort();
    }
    if (!m_done) {
        if (m_db.isOpen()) {
            m_db.getDB().rollback();
        }
        m_db.close();
        QFile::remove(m_partTarget);
    }
}

bool TileLoader::prepare(void)
{
    {
        DataBase aero_db(mapGL::aeroSourceFile());
        if (!aero_db.connect()) {
            fail(0, aero_db.lastError());
            return false;
        }

        QPair<Aerodrom, bool> r = aero_db.from<Aerodrom>()
                                      .select(AerodromFields::long_rad, AerodromFields::lat_rad)
                                      .where(AerodromFields::kod_icao, Comparison::Equal, m_icao).first();
        if (!r.second) {
            fail(0, QStringLiteral("Aerodrom %1 not found").arg(m_icao));
            return false;
        }
        m_lon = qRadiansToDegrees(r.first.long_rad);
        m_lat = qRadiansToDegrees(r.first.lat_rad);
    }

    if (!m_db.createDBStruct<Metadata>()) {
        fail(0, QStringLiteral("Error from Metadata table create: %1").arg(m_db.lastError()));
        return false;
    }
    if (!m_db.createDBStruct<Tiles>()) {
        fail(0, QStringLiteral("Error from Tiles table create: %1").arg(m_db.lastError()));
        return false;
    }
    return true;
}

void TileLoader::init(void)
{
    if (m_target.isEmpty()) {
        fail(0, QStringLiteral("нет каталога для записи карты аэродрома %1").arg(m_icao));
        return;
    }

    QFile::remove(m_partTarget);
    QFile::remove(m_partTarget + QStringLiteral("-journal"));

    if (!m_db.connect()) {
        fail(0, m_db.lastError());
        return;
    }
    prepare();
}

void TileLoader::load(void)
{
    if (m_err) return;

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
    if (m_err) return;

    QUrl requestUrl = mapGL::server::k_serv_url.resolved(QUrl(mapGL::server::k_tile_fetch_postfix));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("lat"), QString::number(m_lat, 'f', 6));
    query.addQueryItem(QStringLiteral("lon"), QString::number(m_lon, 'f', 6));
    query.addQueryItem(QStringLiteral("kod_icao"), m_icao);
    requestUrl.setQuery(query);

    mapGL::debug(QStringLiteral("FETCH %1").arg(requestUrl.toString()));

    QNetworkRequest request(requestUrl);
    request.setTransferTimeout(mapGL::server::k_transfer_timeout_ms);

    QNetworkReply *reply = m_manager->get(request);
    m_fetchReply = reply;
    connect(reply, &QNetworkReply::finished, this, [reply, this]() {
        reply->deleteLater();
        m_fetchReply = nullptr;
        if (m_err) return;

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            // нет связи — повторить через 1, 2, 4, 8, 16 с
            if (status == 0 && m_fetchAttempt < mapGL::server::k_maxAttempts) {
                const int delayMs = 1000 << m_fetchAttempt;
                ++m_fetchAttempt;
                QTimer::singleShot(delayMs, this, &TileLoader::fetchAirportTiles);
                return;
            }
            fail(status, QStringLiteral("HTTP %1: %2").arg(status).arg(reply->errorString()));
            return;
        }
        m_fetchAttempt = 0;

        const QByteArray body = reply->readAll().trimmed();

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
        if (err.error != QJsonParseError::NoError) {
            fail(status, QStringLiteral("pyramid: битый JSON: %1").arg(err.errorString()));
            return;
        }

        if (!doc.isObject()) {
            fail(status, QStringLiteral("pyramid: ждали объект"));
            return;
        }

        if (!parseJsonFetchResponse(doc)) {
            fail(status, QStringLiteral("pyramid: пустой список тайлов"));
            return;
        }

        mapGL::debug(QStringLiteral("pyramid: тайлов в списке %1").arg(m_tileLoadKeys.size()));
        downloadAirportTiles();
    });
}

void TileLoader::downloadAirportTiles(void)
{
    QList<uint64_t> keys(m_tileLoadKeys.cbegin(), m_tileLoadKeys.cend());
    std::sort(keys.begin(), keys.end());
    m_queue = keys;

    if (!m_db.getDB().transaction()) {
        fail(0, m_db.getDB().lastError().text());
        return;
    }
    processQueue();
}

void TileLoader::processQueue(void)
{
    while (!m_err && m_inFlight.size() < mapGL::server::k_max_requested_tile && !m_queue.isEmpty()) {
        const uint64_t key = m_queue.takeFirst();
        if (m_inFlight.contains(key)) {
            continue;
        }
        sendReqForGetTile(key);
    }

    if (!m_err && !m_done && m_queue.isEmpty() && m_inFlight.isEmpty() && m_pendingRetries == 0) {
        finish();
    }
}

void TileLoader::onReplyFinished(uint64_t key, QNetworkReply *reply)
{
    reply->deleteLater();
    if (m_inFlight.value(key) == reply) {
        m_inFlight.remove(key);
    }
    if (m_err) return;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() == QNetworkReply::NoError && status == 200) {
        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty()) {
            m_missing.insert(key);
        } else {
            if (m_format.isEmpty()) {
                m_format = mapGL::format::fromMimeType(
                    reply->header(QNetworkRequest::ContentTypeHeader).toByteArray());
            }
            if (!storeTile(key, bytes)) {
                return;
            }
        }
    } else if (status == 404 || status == 204) {
        // нема тайла - просить больше не надо
        m_missing.insert(key);
    } else {
        // запросить еще раз потом
        const int attempt = ++m_attempts[key];
        if (attempt >= mapGL::server::k_maxAttempts) {
            fail(status, QStringLiteral("HTTP %1: %2").arg(status).arg(reply->errorString()));
            return;
        }
        ++m_pendingRetries;
        QTimer::singleShot(1000 << (attempt - 1), this, [this, key]() {
            --m_pendingRetries;
            if (m_err) return;
            m_queue.push_back(key);
            processQueue();
        });
    }

    processQueue();
}

bool TileLoader::storeTile(uint64_t key, const QByteArray &data)
{
    uint8_t z;
    uint32_t x, y;
    mapGL::math::parseKey(key, &z, &x, &y);

    Tiles tile;
    tile.zoom_level = static_cast<int>(z);
    tile.tile_column = static_cast<int>(x);
    tile.tile_row = static_cast<int>((1u << z) - 1u - y);
    tile.tile_data = data;

    SqlInsertQuery<Tiles> insert = m_db.into<Tiles>();
    if (!insert.insertObject(tile)) {
        fail(0, QStringLiteral("tiles: %1").arg(insert.lastError()));
        return false;
    }
    ++m_stored;

    if (++m_uncommitted >= k_commitBatch) {
        m_uncommitted = 0;
        if (!m_db.getDB().commit() || !m_db.getDB().transaction()) {
            fail(0, m_db.getDB().lastError().text());
            return false;
        }
    }
    return true;
}

bool TileLoader::insertMetaMBTiles(void)
{
    QList<Metadata> tileMeta {
        {QStringLiteral("name"), QStringLiteral("Fallback raster of %1 aerodrom").arg(m_icao)},
        {QStringLiteral("description"),
         QStringLiteral("Offline 256px %1 raster aerodrom: beige land, blue water, green forest/farmland, gray roads")
             .arg(QString::fromLatin1(m_format))},
        {QStringLiteral("type"), QStringLiteral("overlay")},
        {QStringLiteral("version"), m_mapVersion},
        {QStringLiteral("minzoom"), QString::number(m_minzoom)},
        {QStringLiteral("maxzoom"), QString::number(m_maxzoom)},
    };
    if (!m_format.isEmpty()) {
        tileMeta.append({QStringLiteral("format"), QString::fromLatin1(m_format)});
    }
    if (!m_bounds.isEmpty()) {
        tileMeta.append({QStringLiteral("bounds"), m_bounds});
    }

    for (const Metadata &meta : std::as_const(tileMeta)) {
        SqlInsertQuery<Metadata> insert = m_db.into<Metadata>();
        if (!insert.insertObject(meta)) {
            fail(0, QStringLiteral("metadata: %1").arg(insert.lastError()));
            return false;
        }
    }
    return true;
}

void TileLoader::finish(void)
{
    if (m_stored == 0) {
        fail(0, QStringLiteral("сервер не отдал ни одного тайла"));
        return;
    }
    if (!insertMetaMBTiles()) {
        return;
    }
    if (!m_db.getDB().commit()) {
        fail(0, m_db.getDB().lastError().text());
        return;
    }
    m_db.close();

    QFile::remove(m_target);
    if (!QFile::rename(m_partTarget, m_target)) {
        fail(0, QStringLiteral("не удалось сохранить %1").arg(m_target));
        return;
    }
    m_done = true;

    mapGL::debug(QStringLiteral("aerodrom %1: сохранено тайлов %2, нет на сервере %3")
                     .arg(m_icao).arg(m_stored).arg(m_missing.size()));
    emit aerodromTilesReady(m_icao);
}

void TileLoader::fail(int status, const QString &msg)
{
    if (m_err) return;
    m_err = true;
    m_lastError = msg;
    mapGL::warning(QStringLiteral("aerodrom %1: %2").arg(m_icao, m_lastError));

    m_queue.clear();
    cancelAll();
    if (m_fetchReply) {
        m_fetchReply->disconnect(this);
        m_fetchReply->abort();
        m_fetchReply->deleteLater();
        m_fetchReply = nullptr;
    }
    if (m_db.isOpen()) {
        m_db.getDB().rollback();
    }
    m_db.close();
    QFile::remove(m_partTarget);

    emit aerodromTilesFailed(m_icao, status, m_lastError);
}

bool TileLoader::parseJsonFetchResponse(const QJsonDocument &doc)
{
    const QJsonObject rootObj = doc.object();

    m_mapVersion = rootObj.value(QStringLiteral("map_version")).toString();
    m_format = mapGL::format::normalize(rootObj.value(QStringLiteral("format")).toString().toLatin1());

    double minLon = std::numeric_limits<double>::max();
    double minLat = std::numeric_limits<double>::max();
    double maxLon = std::numeric_limits<double>::lowest();
    double maxLat = std::numeric_limits<double>::lowest();

    const QJsonArray layersArray = rootObj.value(QStringLiteral("tiles")).toArray();

    for (const QJsonValue &layerValue : layersArray) {
        const QJsonObject layerObj = layerValue.toObject();
        const int z = layerObj.value(QStringLiteral("z")).toInt(-1);
        if (z < 0 || z > mapGL::k_maxTileZoom)
            continue;
        const uint32_t n = 1u << z;   // тайлов по стороне на уровне z

        const QJsonArray bounds = layerObj.value(QStringLiteral("bounds")).toArray();
        if (bounds.size() == 4) {
            minLon = qMin(minLon, bounds[0].toDouble());
            minLat = qMin(minLat, bounds[1].toDouble());
            maxLon = qMax(maxLon, bounds[2].toDouble());
            maxLat = qMax(maxLat, bounds[3].toDouble());
        }

        for (const QJsonValue &tileValue : layerObj.value(QStringLiteral("tiles")).toArray()) {
            const QJsonArray pair = tileValue.toArray();
            if (pair.size() != 2)
                continue;
            const int x = pair[0].toInt(-1);
            const int y = pair[1].toInt(-1);
            if (x < 0 || y < 0 || uint32_t(x) >= n || uint32_t(y) >= n)
                continue;

            m_tileLoadKeys.insert(mapGL::math::makeKey(z, uint32_t(x), uint32_t(y)));
            m_minzoom = (m_minzoom < 0) ? z : qMin(m_minzoom, z);
            m_maxzoom = qMax(m_maxzoom, z);
        }
    }

    if (minLon < maxLon && minLat < maxLat) {
        m_bounds = QStringLiteral("%1,%2,%3,%4")
                       .arg(minLon, 0, 'f', 6).arg(minLat, 0, 'f', 6)
                       .arg(maxLon, 0, 'f', 6).arg(maxLat, 0, 'f', 6);
    }

    return !m_tileLoadKeys.isEmpty();
}
