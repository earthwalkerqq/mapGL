#ifndef NETWORKTILEPROVIDER_H
#define NETWORKTILEPROVIDER_H

/*
 * класс загрузки тайлов из сети
 */

#include "tileprovider.h"
#include "mapGL.h"

#include <QNetworkAccessManager>
#include <QString>
#include <QUrl>
#include <QQueue>

class NetworkTileProvider : public TileProvider
{
public:
    explicit NetworkTileProvider(QString &serv_addr, QObject *parent = nullptr)
        : m_serv_target(serv_addr), TileProvider{parent} {}

    void load(void) override;
    mapGL::TileQuality quality(void) override { return mapGL::TileQuality::Network; }

private:
    void processQueue(void);
    void sendRequest(const uint64_t &key);

    QUrl m_serv_target;
    QNetworkAccessManager m_manager;
    // очередь запросов, которая ограничивается m_max_requested_tile
    QVector<uint64_t> m_queue;
    QSet<uint64_t> m_requested_tile_keys; // тайлы, которые уже ушли в сеть
    const int m_max_requested_tile = 4;
};

#endif // NETWORKTILEPROVIDER_H
