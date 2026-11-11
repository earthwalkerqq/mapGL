#ifndef TILESCHEDULER_H
#define TILESCHEDULER_H

/*
 * Планировщик задач
 */

#include <QObject>
#include <QCoreApplication>
#include <QCache>

#include "tileprovider.h"
#include "mapGL.h"

class TileScheduler : public QObject
{
    Q_OBJECT
public:
    static TileScheduler *instance()
    {
        static TileScheduler *self = nullptr;
        if (!self)
            self = new TileScheduler(QCoreApplication::instance());
        return self;
    }

    TileScheduler(const TileScheduler&) = delete;
    TileScheduler& operator=(const TileScheduler&) = delete;
    TileScheduler(TileScheduler&&) = delete;
    TileScheduler& operator=(TileScheduler&&) = delete;

    void addSource(std::shared_ptr<TileProvider> src) {
        m_source.push_back(std::move(src));
    }

    void removeSource(mapGL::TileQuality quality) {
        m_source.erase(std::remove_if(m_source.begin(), m_source.end(),
            [quality](std::shared_ptr<TileProvider> src) {
                return quality == src->quality();
            }), m_source.end());
    }

    void asyncTileLoad();

signals:
    void readyTile(mapGL::TileMeta rs);

private:
    explicit TileScheduler(QObject *parent);

    QVector<std::shared_ptr<TileProvider>> m_source;
    QCache<uint64_t, QByteArray> m_cache;
};

#endif // TILESCHEDULER_H
