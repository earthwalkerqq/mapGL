#ifndef TILESERVERMETA_H
#define TILESERVERMETA_H

#include <QNetworkAccessManager>
#include <QUrl>
#include "mapGL.h"

class TileServerMeta
{
protected:
    explicit TileServerMeta(QNetworkAccessManager *manager = nullptr)
        : m_manager(manager) {}

    QNetworkAccessManager *m_manager;
    QList<uint64_t> m_queue;

    // столько держит QNAM на один хост
    static constexpr int k_max_requested_tile = 6;
    static constexpr int k_transfer_timeout_ms = 10000;

    static inline const QUrl m_serv_target = mapGL::k_serv_target;
};

#endif // TILESERVERMETA_H
