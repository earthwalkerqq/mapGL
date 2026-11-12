#ifndef TILEMANAGER_H
#define TILEMANAGER_H

/*
 * связывает камеру, планировщик загрузки и рисование.
 */

#include "sgmapview.h"
#include "tilestore.h"
#include "tilescheduler.h"

#include <memory>

class TileManager : public SGMapView
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit TileManager(QQuickItem *parent = nullptr);
    ~TileManager() override;

protected:
    void onCameraChanged() override;
    bool tileImage(uint64_t key, QImage *img, quint32 *version) const override;

private:
    // пересчитать набор нужных тайлов (не чаще раза за проход цикла событий)
    void reloadTiles();

    // планировщик позже хранилища - удалиться раньше
    TileStore m_store;
    std::unique_ptr<TileScheduler> m_scheduler;

    QVector<uint64_t> m_lastKeys;
    bool m_reloadPending = false;
};

#endif // TILEMANAGER_H
