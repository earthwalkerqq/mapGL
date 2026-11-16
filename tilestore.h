#ifndef TILESTORE_H
#define TILESTORE_H

/*
 * Хранилище готовых тайлов в виде QImage
 *
 * - insert()  — только главный поток (из TileScheduler);
 * - get()     — поток рендера (updatePaintNode), главный в это время стоит;
 * - has()     — потоки провайдеров (не запрашивать то, что уже есть);
 *
 * каждая вставка получает новый номер. По нему SGMapView понимает,
 * что под тем же ключом лежит уже другая картинка (пришла лучше качеством),
 * и пересоздаёт текстуру.
 *
 * один сторе на все слои нельзя, у раста и вектора могут быть
 * один и тот же ключ - конфликт
 */

#include "mapGL.h"

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>

template<typename CacheTarget>
class TileStore
{
public:
    explicit TileStore(qsizetype maxBytes) : m_cache(maxBytes) {}

    TileStore(const TileStore &) = delete;
    TileStore &operator=(const TileStore &) = delete;

    /*
     * @return true — картинка легла в хранилище (надо перерисовать).
     * Худшее качество не вытесняет лучшее; равное — заменяет
     */
    bool insert(uint64_t key, const QImage &img, mapGL::TileQuality quality)
    {
        if (img.isNull())
            return false;

        QMutexLocker locker(&m_mtx);
        if (const CacheTarget *e = m_cache.object(key)) {
            if (e->tile.quality > quality)
                return false;
        }
        const qsizetype cost = img.sizeInBytes() + qsizetype(sizeof(CacheTarget));
        return m_cache.insert(key, new CacheTarget{mapGL::DataTile{img, quality}, m_nextVersion++}, cost);
    }

    /*
     * img может быть nullptr — тогда только проверка наличия и версия.
     * QImage копируется по значению: это неявное разделение данных
     * (атомарный счётчик), пикселы не копируются. Указатель на элемент
     * QCache наружу отдавать нельзя: его может вытеснить вставка из другого потока.
     */
    bool get(uint64_t key, QImage *img, quint32 *version) const
    {
        QMutexLocker locker(&m_mtx);
        const CacheTarget *e = m_cache.object(key);
        if (!e)
            return false;
        if (img)
            *img = e->tile.img;
        if (version)
            *version = e->version;
        return true;
    }

    // есть ли тайл не хуже minQuality — чтобы не грузить его повторно
    bool has(uint64_t key, mapGL::TileQuality minQuality) const
    {
        QMutexLocker locker(&m_mtx);
        const CacheTarget *e = m_cache.object(key);
        return e && e->tile.quality >= minQuality;
    }

    void removeQuality(mapGL::TileQuality quality)
    {
        QMutexLocker locker(&m_mtx);
        const QList<uint64_t> keys = m_cache.keys();
        for (const uint64_t key : keys) {
            const CacheTarget *e = m_cache.object(key);
            if (e && e->tile.quality == quality) {
                m_cache.remove(key);
            }
        }
    }

private:
    mutable QMutex m_mtx;
    QCache<uint64_t, CacheTarget> m_cache;
    quint32 m_nextVersion = 1;
};

struct EntryRaster {
    mapGL::DataTile tile;
    quint32 version;
};

struct EntryVector {
    mapGL::DataTile tile;
    quint32 version;
};

class TileStoreRaster : public TileStore<EntryRaster>
{
public:
    using TileStore<EntryRaster>::TileStore;
};

class TileStoreVector : public TileStore<EntryVector>
{
public:
    using TileStore<EntryVector>::TileStore;
};

#endif // TILESTORE_H
