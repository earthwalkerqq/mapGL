#ifndef SERVERMETA_H
#define SERVERMETA_H

#include <QString>
#include <QUrl>

namespace mapGL {

namespace server {

const QString k_serv_target = QStringLiteral("http://172.20.10.2:8080");
const QString k_tile_req_postfix = QStringLiteral("/tiles");
const QString k_tile_fetch_postfix = QStringLiteral("/tiles/piramid");

// столько держит QNAM на один хост
constexpr int k_max_requested_tile = 6;
constexpr int k_transfer_timeout_ms = 10000;

// максимальное кол-во ожиданий сети
constexpr int k_maxAttempts = 5;

const QUrl k_serv_url(k_serv_target);

} // namespace server

} // namespace mapGL

#endif // SERVERMETA_H
