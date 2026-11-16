#ifndef TILESCHEDULER_H
#define TILESCHEDULER_H

/*
 * Планировщик задач загрузки
 */

#include "tiledecoder.h"
#include "tileprovider.h"
#include "tilestore.h"
#include "mapGL.h"

#include <QHash>
#include <QObject>
#include <QVector>

class QThread;

class TileScheduler : public QObject
{
    Q_OBJECT
public:
    explicit TileScheduler(TileStoreRaster *storeRaster, TileStoreVector *storeVector, QObject *parent = nullptr);
    ~TileScheduler() override;

    TileScheduler(const TileScheduler&) = delete;
    TileScheduler& operator=(const TileScheduler&) = delete;

    void addSource(TileProvider *src);
    void removeSource(mapGL::TileQuality quality);
    void removeSource(TileProvider *src);

    void setVisibleTiles(const QVector<uint64_t> &rasterKeys, const QVector<uint64_t> &vectorKeys);

signals:
    // в TileStore появились новые картинки — пора перерисоваться
    void tilesChanged(void);

private:
    struct Source
    {
        TileProvider *provider = nullptr;
        QThread *thread = nullptr; // nullptr — провайдер живёт в главном потоке
    };

    void requestLoad(const Source &src, quint64 generation);
    void destroySource(const Source &src);
    void dropDecoding(mapGL::TileQuality quality, bool vector);

    void onRawTileRaster(const mapGL::TileMeta &tile);
    void onRawTileVector(const mapGL::TileMeta &tile);
    void onDecodedRaster(uint64_t key, const QImage &img, mapGL::TileQuality quality);
    void onDecodedVector(uint64_t key, const QImage &img, mapGL::TileQuality quality);
    void onFailedTile(uint64_t key, const QString &msg);

    TileStoreRaster *m_storeRaster;
    TileStoreVector *m_storeVector;
    TileDecoder m_decoder;
    QVector<Source> m_source;
    // ключи, которые сейчас декодируются, и с каким качеством — чтобы не декодировать дважды
    QHash<uint64_t, mapGL::TileQuality> m_decodingRaster;
    QHash<uint64_t, mapGL::TileQuality> m_decodingVector;
    QVector<uint64_t> m_visibleRaster;
    QVector<uint64_t> m_visibleVector;
    quint64 m_generation = 0;
};

#endif // TILESCHEDULER_H
