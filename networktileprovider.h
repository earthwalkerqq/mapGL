#ifndef NETWORKTILEPROVIDER_H
#define NETWORKTILEPROVIDER_H

/*
 * класс загрузки тайлов из сети
 */

#include "tileprovider.h"

#include <QNetworkAccessManager>
#include <QString>
#include <QUrl>

class NetworkTileProvider : public TileProvider
{
public:
    explicit NetworkTileProvider(QString &serv_addr, QObject *parent = nullptr)
        : m_serv_target(serv_addr), TileProvider{parent} {}

    void load(uint64_t key) override;

private:
    QUrl m_serv_target;
    QNetworkAccessManager m_manager;
};

#endif // NETWORKTILEPROVIDER_H
