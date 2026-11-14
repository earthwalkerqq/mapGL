#ifndef TILELOADER_H
#define TILELOADER_H

#include <QObject>
#include <QString>
#include <QJsonDocument>
#include <QSet>
#include "servermeta.h"
#include "orm.h"
#include "network.h"

class TileLoader
    : public QObject, private Network
{
    Q_OBJECT
public:
    explicit TileLoader(const QString &trg, QObject *parent = nullptr);
    ~TileLoader(void);

    void init(void);
    void load(void);

signals:
    void aerodromTilesFailed(int status, QString &msg);

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
    void parseJsonFetchResponse(const QJsonDocument &doc);
    void prepare(void);

    QString m_icao;
    QString m_target;
    DataBase m_db;
    bool m_err = false;
    QString m_lastError;

    double m_lat;
    double m_lon;
    int m_minzoom;
    int m_maxzoom;
    QString m_mapVersion;

    int m_fetchAttempt = 0;

    QSet<uint64_t> m_tileLoadKeys; // талйлы необходимые для скачивания
};

#endif // TILELOADER_H
