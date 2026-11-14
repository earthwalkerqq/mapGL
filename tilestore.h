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
 */

#include "mapGL.h"

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>

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
        if (const Entry *e = m_cache.object(key)) {
            if (e->tile.quality > quality)
                return false;
        }
        const qsizetype cost = img.sizeInBytes() + qsizetype(sizeof(Entry));
        return m_cache.insert(key, new Entry{mapGL::DataTile{img, quality}, m_nextVersion++}, cost);
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
        const Entry *e = m_cache.object(key);
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
        const Entry *e = m_cache.object(key);
        return e && e->tile.quality >= minQuality;
    }

private:
    struct Entry
    {
        mapGL::DataTile tile;
        quint32 version;
    };

    mutable QMutex m_mtx;
    QCache<uint64_t, Entry> m_cache;
    quint32 m_nextVersion = 1;
};

#endif // TILESTORE_H
