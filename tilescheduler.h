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
    explicit TileScheduler(TileStore *store, QObject *parent = nullptr);
    ~TileScheduler() override;

    TileScheduler(const TileScheduler&) = delete;
    TileScheduler& operator=(const TileScheduler&) = delete;

    void addSource(TileProvider *src);
    void removeSource(mapGL::TileQuality quality);
    void removeSource(TileProvider *src);

    void setVisibleTiles(const QVector<uint64_t> &keys);

signals:
    // в TileStore появились новые картинки — пора перерисоваться
    void tilesChanged(void);

private:
    struct Source
    {
        TileProvider *provider = nullptr;
        QThread *thread = nullptr; // nullptr — провайдер живёт в главном потоке
    };

    void requestLoad(const Source &src, const QVector<uint64_t> &keys, quint64 generation);
    void destroySource(const Source &src);

    void onRawTile(const mapGL::TileMeta &tile);
    void onDecoded(uint64_t key, const QImage &img, mapGL::TileQuality quality);
    void onFailedTile(uint64_t key, const QString &msg);

    TileStore *m_store;
    TileDecoder m_decoder;
    QVector<Source> m_source;
    // ключи, которые сейчас декодируются, и с каким качеством — чтобы не декодировать дважды
    QHash<uint64_t, mapGL::TileQuality> m_decoding;
    QVector<uint64_t> m_visible;
    quint64 m_generation = 0;
};

#endif // TILESCHEDULER_H
