#ifndef TILEMANAGER_H
#define TILEMANAGER_H

#include "sgmapview.h"
#include "tilescheduler.h"

class TileManager : public SGMapView
{
    Q_OBJECT
public:
    explicit TileManager(QQuickItem *parent = nullptr)
        : SGMapView(parent)
    {
        m_scheduler = TileScheduler::instance();
        // TODO: Добавить сеть и подложку
        m_scheduler->addSource(std::make_shared<TileProvider>());
        m_scheduler->addSource(std::make_shared<TileProvider>());
        connect();
    }

protected:
    void onCameraChanged() override
    {
        if (!m_camera) return;
        const QVector<VisibleTile> visible = m_camera->visibleTiles();

        std::vector<uint64_t> keys;

        for (const VisibleTile &t : visible)
            keys.push_back(TileCore::makeKey(t.z, t.x, t.y));

        for (const VisibleTile &t : visible) {
            if (t.z <= m_BaseMaxZoom)
                continue;
            const int up = t.z - m_BaseMaxZoom;
            const uint64_t base = TileCore::makeKey(m_BaseMaxZoom, t.x >> up, t.y >> up);
            if (std::find(keys.begin(), keys.end(), base) == keys.end())
                keys.push_back(base);
        }

        m_tiles.load_tiles(keys);
        update();
    }

    // рисует MapLayer текстурами; здесь — только откуда брать картинку тайла
    const QImage *tileImage(uint64_t key) const override { return m_tiles.getImage(key); }

private:
    TileScheduler *m_scheduler;
};

#endif // TILEMANAGER_H
