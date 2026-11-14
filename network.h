#ifndef NETWORK_H
#define NETWORK_H

#include <QObject>
#include <QUrl>
#include <QString>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QHash>
#include <QSet>
#include "servermeta.h"
#include "mapGL.h"

class Network
{
protected:
    explicit Network(QObject *context = nullptr)
        : m_context(context), m_manager(new QNetworkAccessManager(context)) {}

    void sendReqForGetTile(uint64_t key)
    {
        uint8_t z;
        uint32_t x, y;
        mapGL::math::parseKey(key, &z, &x, &y);

        const QUrl requestUrl = QUrl(getReqUrlTarget(static_cast<int>(z), static_cast<int>(x), static_cast<int>(y)));

        mapGL::debug(QStringLiteral("GET - %1").arg(requestUrl.toString()));

        QNetworkRequest request(requestUrl);
        // без таймаута на плохой сети слот может висеть минутами
        request.setTransferTimeout(mapGL::server::k_transfer_timeout_ms);

        QNetworkReply *reply = m_manager->get(request);
        m_inFlight.insert(key, reply);
        QObject::connect(reply, &QNetworkReply::finished, m_context, [this, key, reply]() {
            onReplyFinished(key, reply);
        });
    }

    virtual QString getReqUrlTarget(const int z, const int x, const int y) = 0;
    virtual void onReplyFinished(const uint64_t key, QNetworkReply * reply) = 0;

    QNetworkAccessManager *m_manager;
    QList<uint64_t> m_queue; // очередь тайлов

    QHash<uint64_t, QNetworkReply *> m_inFlight; // тайлы, которые уже ушли в сеть
    QSet<uint64_t> m_cancelled; // старые тайлы, которые уже не попадают в экран
    QSet<uint64_t> m_missing;   // 404 тайлы

private:
    QObject *m_context = nullptr;
};

#endif // NETWORK_H
