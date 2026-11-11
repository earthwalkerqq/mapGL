#ifndef TILEPROVIDER_H
#define TILEPROVIDER_H

/*
 * абсрактный класс с общим интерфейсом
 * взятия тайлов из неопределенного источника
 */

#include "mapGL.h"
#include <QObject>
#include <QReadWriteLock>

class TileProvider : public QObject
{
    Q_OBJECT

public:
    TileProvider(QObject *parent = nullptr)
        : QObject(parent) {}

    virtual void load(void) = 0;
    virtual mapGL::TileQuality quality(void);

    static void setVisibleTiles(QVector<uint64_t> &&keys)
    {
        QWriteLocker locker(&m_lock);
        m_visibleTile = std::move(keys);
    }

    static QVector<uint64_t> getVisibleTiles() {
        QReadLocker locker(&m_lock);
        return m_visibleTile;
    }

signals:
    void readyTile(mapGL::TileMeta rs);
    void failedTile(uint64_t key, const QString msgError);

protected:
    /*
     * TODO:
     * как то надо использовать пулл тайлов,
     * которые нужно загрузить
     */
    static QVector<uint64_t> m_visibleTile;
    static QReadWriteLock m_lock;
};

#endif // TILEPROVIDER_H
