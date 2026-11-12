#ifndef TILELOADER_H
#define TILELOADER_H

#include <QObject>
#include <QJsonDocument>
#include "tileservermeta.h"

class TileLoader
    : public QObject, private TileServerMeta
{
    Q_OBJECT
public:
    TileLoader(QObject *parent = nullptr);

private:
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
};

#endif // TILELOADER_H
