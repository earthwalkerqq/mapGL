#include "networktileprovider.h"
#include "mapGL.h"

#include <QNetworkReply>

static inline QString getReqUrlTarget(int z, int x, int y)
{
    QString postfix = QStringLiteral("%1/%2/%3/%4")
        .arg(mapGL::k_tile_req_postfix).arg(int(z)).arg(x).arg(y);

    return postfix;
}

void NetworkTileProvider::load(uint64_t key)
{
    uint8_t z;
    uint32_t x, y;
    mapGL::math::parseKey(key, &z, &x, &y);

    QString reqPostfix = getReqUrlTarget(static_cast<int>(z), x, y);
    const QUrl requestUrl = m_serv_target.resolved(QUrl(reqPostfix));

    mapGL::debug(QStringLiteral("GET - %1").arg(requestUrl.toString()));

    QNetworkReply *reply = m_manager.get(QNetworkRequest(requestUrl));

    connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            emit failedTile(key, QStringLiteral("HTTP %1: %2")
                .arg(status)
                .arg(reply->errorString()));
            return;
        }

        QByteArray bytes = reply->readAll();

        if (bytes.isEmpty()) {
            emit failedTile(key, QStringLiteral("HTTP %1: Empty response").arg(status));
        } else {
            emit readyTile({key, std::move(bytes), mapGL::TileQuality::Network});
        }
    });
}
