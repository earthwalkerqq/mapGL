#include "networktileprovider.h"
#include "mapGL.h"
#include "tileformat.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

static inline QString getReqUrlTarget(int z, int x, int y)
{
    QString postfix = QStringLiteral("%1/%2/%3/%4")
        .arg(mapGL::k_tile_req_postfix).arg(z).arg(x).arg(y);

    return postfix;
}

NetworkTileProvider::NetworkTileProvider(QObject *parent)
    : TileProvider{parent},
    TileServerMeta(new QNetworkAccessManager(this)) {}

NetworkTileProvider::~NetworkTileProvider()
{
    const QList<QNetworkReply *> replies = m_inFlight.values();
    m_inFlight.clear();
    for (QNetworkReply *reply : replies) {
        reply->disconnect(this);
        reply->abort();
    }
}

void NetworkTileProvider::load(const QVector<uint64_t> &keys, quint64 generation)
{
    if (isStale(generation)) {
        return;
    }

    // новая очередь: только то, чего нет ни в хранилище, ни в полёте
    QSet<uint64_t> wanted;
    wanted.reserve(keys.size());
    m_queue.clear();
    for (const uint64_t key : keys) {
        wanted.insert(key);
        if (m_inFlight.contains(key) || m_missing.contains(key) || alreadyHave(key)) {
            continue;
        }
        m_queue.push_back(key);
    }

    /*
     * Запросы за тайлами, которые ушли с экрана, отменяем — освобождаем слоты
     * под нужные. abort() вызывает finished() синхронно, а тот меняет
     * m_inFlight, поэтому сначала собираем список, потом отменяем.
     */
    QList<QNetworkReply *> stale;
    for (auto it = m_inFlight.cbegin(); it != m_inFlight.cend(); ++it) {
        if (!wanted.contains(it.key())) {
            m_cancelled.insert(it.key());
            stale.push_back(it.value());
        }
    }
    for (QNetworkReply *reply : std::as_const(stale)) {
        reply->disconnect();
        reply->abort();
    }

    processQueue();
}

void NetworkTileProvider::processQueue()
{
    while (m_inFlight.size() < k_max_requested_tile && !m_queue.isEmpty()) {
        const uint64_t key = m_queue.takeFirst();
        if (m_inFlight.contains(key) || m_missing.contains(key) || alreadyHave(key)) {
            continue;
        }
        sendRequest(key);
    }
}

void NetworkTileProvider::sendRequest(uint64_t key)
{
    uint8_t z;
    uint32_t x, y;
    mapGL::math::parseKey(key, &z, &x, &y);

    const QString reqPostfix = getReqUrlTarget(static_cast<int>(z), static_cast<int>(x), static_cast<int>(y));
    const QUrl requestUrl = m_serv_target.resolved(QUrl(reqPostfix));

    mapGL::debug(QStringLiteral("GET - %1").arg(requestUrl.toString()));

    QNetworkRequest request(requestUrl);
    // без таймаута на плохой сети слот может висеть минутами
    request.setTransferTimeout(k_transfer_timeout_ms);

    QNetworkReply *reply = m_manager->get(request);
    m_inFlight.insert(key, reply);
    connect(reply, &QNetworkReply::finished, this, [this, key, reply]() {
        onReplyFinished(key, reply);
    });
}

void NetworkTileProvider::onReplyFinished(uint64_t key, QNetworkReply *reply)
{
    reply->deleteLater();
    if (m_inFlight.value(key) == reply) {
        m_inFlight.remove(key);
    }

    const bool cancelledByUs = m_cancelled.remove(key);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (cancelledByUs) {
    } else if (status == 404 || status == 204) {
        m_missing.insert(key);
        emit failedTile(key, QStringLiteral("HTTP %1: тайла нет на сервере").arg(status));
    } else if (reply->error() != QNetworkReply::NoError) {
        emit failedTile(key, QStringLiteral("HTTP %1: %2").arg(status).arg(reply->errorString()));
    } else if (status != 200) {
        emit failedTile(key, QStringLiteral("HTTP %1: неожиданный ответ").arg(status));
    } else {
        QByteArray bytes = reply->readAll();
        if (bytes.isEmpty()) {
            emit failedTile(key, QStringLiteral("HTTP %1: пустой ответ").arg(status));
        } else {
            const QByteArray format = mapGL::format::fromMimeType(
                reply->header(QNetworkRequest::ContentTypeHeader).toByteArray());
            emit readyTile(mapGL::TileMeta{key, mapGL::RawTile{std::move(bytes), quality(), format}});
        }
    }

    processQueue();
}
