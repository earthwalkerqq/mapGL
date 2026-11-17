#ifndef TILELOADER_H
#define TILELOADER_H

#include <QObject>
#include <QString>
#include <QJsonDocument>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QNetworkReply>
#include "servermeta.h"
#include "orm.h"
#include "networkservice.h"

class TileLoader
    : public QObject, private NetworkService
{
    Q_OBJECT
public:
    TileLoader(const QString &icao, const QString &target, QObject *parent = nullptr);
    ~TileLoader(void) override;

    void init(void);
    void load(void);

    QString icao(void) const { return m_icao; }

signals:
    void aerodromTilesReady(const QString &icao);
    void aerodromTilesFailed(const QString &icao, int status, const QString &msg);

private:
    void onReplyFinished(const uint64_t key, QNetworkReply *reply) override;
    QString getReqUrlTarget(const int z, const int x, const int y) override
    {
        return QStringLiteral("%1%2/%3/%4/%5")
            .arg(mapGL::server::k_serv_target,
                 mapGL::server::k_tile_req_postfix,
                 QString::number(z),
                 QString::number(x),
                 QString::number(y));
    }
    /*
     * Первый запрос на сервер - запрос на список
     * тайлов, которые нужны на аэродром.
     * Получаю JSON {
     * "map_version": "example-etag",
     * "kod_icao": "UUDD",
     * "tiles": [{
     *      "z": 7,
     *      "bounds": [32.818975, 53.057839, 42.415625, 58.453761],
     *      "tiles": [[77, 40]]
     *      },{
     *      "z": 8,
     *      "bounds": [34.419839, 53.957159, 40.814761, 57.554441],
     *      "tiles": [[154, 80], [154, 79]]
     *      }, ...
     * ]}
     */
    void fetchAirportTiles(void);
    bool parseJsonFetchResponse(const QJsonDocument &doc);
    bool prepare(void);
    bool insertMetaMBTiles(void);
    void downloadAirportTiles(void);
    void processQueue(void);
    bool storeTile(uint64_t key, const QByteArray &data);
    void finish(void);
    void fail(int status, const QString &msg);

    QString m_icao;
    QString m_target;
    QString m_partTarget;
    DataBase m_db;
    bool m_err = false;
    bool m_done = false;
    QString m_lastError;

    double m_lat = 0.;
    double m_lon = 0.;
    int m_minzoom = -1;
    int m_maxzoom = -1;
    QByteArray m_format;
    QString m_mapVersion;
    QString m_bounds;

    int m_fetchAttempt = 0;
    QPointer<QNetworkReply> m_fetchReply;

    QSet<uint64_t> m_tileLoadKeys; // талйлы необходимые для скачивания
    QHash<uint64_t, int> m_attempts;
    int m_pendingRetries = 0;
    int m_uncommitted = 0;
    int m_stored = 0;
};

#endif // TILELOADER_H
