#ifndef NETWORKTILEPROVIDER_H
#define NETWORKTILEPROVIDER_H

/*
 * Класс загрузки тайлов из сети.
 *
 * QNAM асинхронный, поэтому может выполняться
 * и в главном потоке
 */

#include "tileprovider.h"
#include "tileservermeta.h";
#include "mapGL.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

class NetworkTileProvider
    : public TileProvider, private TileServerMeta
{
    Q_OBJECT
public:
    explicit NetworkTileProvider(QObject *parent = nullptr);
    ~NetworkTileProvider() override;

    mapGL::TileQuality quality(void) const override { return mapGL::TileQuality::Network; }

    void load(const QVector<uint64_t> &keys, quint64 generation) override;

private:
    void processQueue(void);
    void sendRequest(uint64_t key);
    void onReplyFinished(uint64_t key, QNetworkReply *reply);

    QHash<uint64_t, QNetworkReply *> m_inFlight; // тайлы, которые уже ушли в сеть
    QSet<uint64_t> m_cancelled; // старые тайлы, которые уже не попадают в экран
    QSet<uint64_t> m_missing;   // 404 тайлы
};

#endif // NETWORKTILEPROVIDER_H
