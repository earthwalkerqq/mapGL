#ifndef TILEPROVIDER_H
#define TILEPROVIDER_H

/*
 * абсрактный класс с общим интерфейсом
 * взятия тайлов из неопределенного источника
 */

#include "mapGL.h"
#include <QObject>

class TileProvider : public QObject
{
    Q_OBJECT

public:
    TileProvider(QObject *parent = nullptr)
        : QObject(parent) {}

    virtual void load(uint64_t key) = 0;

signals:
    void readyTile(mapGL::TileMeta rs);
    void failedTile(uint64_t key, const QString msgError);
};

#endif // TILEPROVIDER_H
