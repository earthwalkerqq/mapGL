#include "networktileprovider.h"
#include "mapGL.h"

#include <QNetworkReply>

static inline QString getReqUrlTarget(int z, int x, int y)
{
    QString postfix = QStringLiteral("%1/%2/%3/%4")
        .arg(mapGL::k_tile_req_postfix).arg(int(z)).arg(x).arg(y);

    return postfix;
}

void NetworkTileProvider::load(void)
{
    m_queue.clear();
    const QVector<uint64_t> keys = getVisibleTiles();

    m_queue.reserve(keys.size());

    // for (const uint64_t &key : keys)
    // {
    //     /*
    //      * TODO: добавить кэш
    //      * Тогда необходимо будет добавлять в очередь по
    //      * одному тайлу, ведь какие-то будут в кэше
    //      * m_queue.push_back(key);
    //      */
    // }
    std::copy(keys.begin(), keys.end(), std::back_inserter(m_queue));
}

void NetworkTileProvider::processQueue()
{
    while (m_requested_tile_keys.size() < m_max_requested_tile && !m_queue.isEmpty()) {
        const uint64_t key = m_queue.takeLast();
        if (m_requested_tile_keys.contains(key)) {
            continue;
        }
        m_requested_tile_keys.insert(key);
        sendRequest(key);
    }
}

void NetworkTileProvider::sendRequest(const uint64_t &key)
{
    uint8_t z;
    uint32_t x, y;
    mapGL::math::parseKey(key, &z, &x, &y);

    QString reqPostfix = getReqUrlTarget(static_cast<int>(z), x, y);
    const QUrl requestUrl = m_serv_target.resolved(QUrl(reqPostfix));

    mapGL::debug(QStringLiteral("GET - %1").arg(requestUrl.toString()));

    QNetworkReply *reply = m_manager.get(QNetworkRequest(requestUrl));
    m_requested_tile_keys.insert(key);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            emit failedTile(key, QStringLiteral("HTTP %1: %2")
                                .arg(status)
                                .arg(reply->errorString()));
            m_requested_tile_keys.remove(key);
            return;
        }

        QByteArray bytes = reply->readAll();

        if (bytes.isEmpty()) {
            emit failedTile(key, QStringLiteral("HTTP %1: Empty response").arg(status));
        } else {
            emit readyTile({key, std::move(bytes), quality()});
        }
        m_requested_tile_keys.remove(key);
        processQueue();
    });
}
