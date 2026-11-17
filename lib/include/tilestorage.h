#ifndef TILESTORAGE_H
#define TILESTORAGE_H

#include <QObject>
#include <QByteArray>
#include <QSet>
#include <QString>
#include <QVector>

#include "tileprovider.h"
#include "orm.h"

class TileStorage : public TileProvider
{
    Q_OBJECT

public:
    TileStorage(const TileStorage &) = delete;
    TileStorage &operator=(const TileStorage &) = delete;

    bool needsOwnThread(void) const override { return true; }
    void init(void) override;

    QString target(void) const { return m_target; }

protected:
    TileStorage(const QString &target, int defaultMaxZoom, QObject *parent = nullptr)
        : TileProvider{parent}, m_target(target), m_db(m_target), m_defaultMaxZoom(defaultMaxZoom) {}

    virtual void prepare(void) = 0;

    void readMetadata(void);
    QVector<uint64_t> storageKeys(const QVector<uint64_t> &keys) const;
    bool readTile(uint64_t key, QByteArray *data);
    void setError(const QString &msg);

    QString m_target;
    DataBase m_db;

    // пусто — в metadata не указаны
    int m_minzoom = -1;
    int m_maxzoom = -1;
    int m_defaultMaxZoom;
    QByteArray m_format;

    bool m_hasBounds = false;
    double m_minLon = -180.0;
    double m_minLat = -90.0;
    double m_maxLon = 180.0;
    double m_maxLat = 90.0;

    bool m_err = false;
    QString m_lastError;
    // тайлы, которых нет в mbtiles
    QSet<uint64_t> m_set_miss;

private:
    bool inBounds(uint8_t z, uint32_t x, uint32_t y) const;
};

#endif // TILESTORAGE_H
