#include "tilemanager.h"

#include "fallbacktileprovider.h"
#include "networktileprovider.h"
#include "mapGL.h"

#include <QSet>

TileManager::TileManager(QQuickItem *parent)
    : SGMapView(parent)
    , m_store(static_cast<qsizetype>(mapGL::convertToBytes(30., mapGL::DataUnit::MB)))
    , m_scheduler(std::make_unique<TileScheduler>(&m_store))
{
    m_scheduler->addSource(new FallbackTileProvider(mapGL::k_fallback_target));
    m_scheduler->addSource(new NetworkTileProvider());
    connect(m_scheduler.get(), &TileScheduler::tilesChanged, this, [this]() { update(); });

    // камера одна на слой; создаём после планировщика: setCamera() сразу запросит тайлы
    setCamera(new CameraView(this));
}

TileManager::~TileManager()
{
    // явно: остановить потоки до разрушения m_store
    m_scheduler.reset();
}

void TileManager::onCameraChanged()
{
    // перерисовать — сразу (сдвиг уже загруженных текстур)
    update();

    /*
     * А вот пересчёт набора тайлов — один раз на проход цикла событий:
     * при перетаскивании cameraChanged приходит на каждый пиксель.
     */
    if (!m_reloadPending) {
        m_reloadPending = true;
        QMetaObject::invokeMethod(this, [this]() { reloadTiles(); }, Qt::QueuedConnection);
    }
}

void TileManager::reloadTiles()
{
    m_reloadPending = false;
    if (!m_camera) return;

    /*
     * На мелких зумах мир уже экрана и один тайл виден несколько раз:
     * для рисования это нужно, для загрузки — нет. Схлопываем, сохраняя
     * порядок приоритета.
     */
    QVector<uint64_t> keys;
    QSet<uint64_t> seen;
    for (const VisibleTile &t : tilesToShow()) {
        const uint64_t key = mapGL::math::makeKey(t.z, t.x, t.y);
        if (!seen.contains(key)) {
            seen.insert(key);
            keys.push_back(key);
        }
    }

    // сдвинулись внутри тех же тайлов — грузить нечего
    if (keys == m_lastKeys) return;
    m_lastKeys = keys;

    m_scheduler->setVisibleTiles(keys);
}

bool TileManager::tileImage(uint64_t key, QImage *img, quint32 *version) const
{
    return m_store.get(key, img, version);
}
