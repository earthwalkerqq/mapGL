#ifndef NETWORKTILEPROVIDER_H
#define NETWORKTILEPROVIDER_H

/*
 * Класс загрузки тайлов из сети.
 *
 * QNAM асинхронный, поэтому может выполняться
 * и в главном потоке
 */

#include "tileprovider.h"
#include "network.h"
#include "mapGL.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

class NetworkTileProvider
    : public TileProvider, private Network
{
    Q_OBJECT
public:
    explicit NetworkTileProvider(QObject *parent = nullptr);
    ~NetworkTileProvider() override;

    mapGL::TileQuality quality(void) const override { return mapGL::TileQuality::Network; }

    void load(const QVector<uint64_t> &keys, quint64 generation) override;

private slots:
    void onReplyFinished(const uint64_t key, QNetworkReply * reply) override;

private:
    QString getReqUrlTarget(const int z, const int x, const int y) override
    {
        return QStringLiteral("%1%2/%3/%4/%5")
            .arg(mapGL::server::k_serv_target,
            mapGL::server::k_tile_req_postfix,
            QString::number(z),
            QString::number(x),
            QString::number(y));
    }

    void processQueue(void);
};

#endif // NETWORKTILEPROVIDER_H
