#include "tilemanager.h"

#include "aerodromtilerasterprovider.h"
#include "fallbacktileprovider.h"
#include "networktileprovider.h"
#include "standartsourcepath.h"
#include "tileloader.h"
#include "mapGL.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QThread>

TileManager::TileManager(QQuickItem *parent)
    : SGMapView(parent)
    , m_store(static_cast<qsizetype>(mapGL::convertToBytes(30., mapGL::DataUnit::MB)))
    , m_scheduler(std::make_unique<TileScheduler>(&m_store))
{
    m_scheduler->addSource(new FallbackTileProvider(mapGL::fallbackTileFile()));
    m_scheduler->addSource(new NetworkTileProvider());
    connect(m_scheduler.get(), &TileScheduler::tilesChanged, this, [this]() { update(); });

    setCamera(new CameraView(this));
}

TileManager::~TileManager()
{
    for (const Loader &l : std::as_const(m_loaders)) {
        disconnect(l.loader, nullptr, this, nullptr);
        l.thread->quit();
        l.thread->wait();
        delete l.thread;
    }
    m_loaders.clear();

    m_scheduler.reset();
}

QString TileManager::normalizeIcao(const QString &icao)
{
    static const QRegularExpression re(QStringLiteral("^\\w{2,8}$"),
                                       QRegularExpression::UseUnicodePropertiesOption);
    const QString code = icao.trimmed().toUpper();
    return re.match(code).hasMatch() ? code : QString();
}

bool TileManager::isAerodromOpened(const QString &icao) const
{
    return m_aerodroms.contains(normalizeIcao(icao));
}

bool TileManager::isAerodromLoading(const QString &icao) const
{
    return m_loaders.contains(normalizeIcao(icao));
}

bool TileManager::openAerodrom(const QString &icao)
{
    const QString code = normalizeIcao(icao);
    if (code.isEmpty()) {
        mapGL::warning(QStringLiteral("некорректный код аэродрома: %1").arg(icao));
        return false;
    }
    if (m_aerodroms.contains(code)) {
        return true;
    }

    const QString path = mapGL::aeroDB(code);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return false;
    }

    addAerodromSource(code);
    return true;
}

bool TileManager::downloadAerodrom(const QString &icao)
{
    const QString code = normalizeIcao(icao);
    if (code.isEmpty()) {
        mapGL::warning(QStringLiteral("некорректный код аэродрома: %1").arg(icao));
        return false;
    }
    if (m_loaders.contains(code)) {
        return true;
    }

    Loader l;
    l.loader = new TileLoader(code, mapGL::aeroDB(code));
    l.thread = new QThread;
    l.thread->setObjectName(QStringLiteral("TileLoader-%1").arg(code));
    l.loader->moveToThread(l.thread);

    connect(l.thread, &QThread::finished, l.loader, &QObject::deleteLater);
    connect(l.loader, &TileLoader::aerodromTilesReady, this, &TileManager::onAerodromTilesReady);
    connect(l.loader, &TileLoader::aerodromTilesFailed, this, &TileManager::onAerodromTilesFailed);

    l.thread->start();

    TileLoader *loader = l.loader;
    QMetaObject::invokeMethod(loader, [loader]() { loader->init(); }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(loader, [loader]() { loader->load(); }, Qt::QueuedConnection);

    m_loaders.insert(code, l);
    return true;
}

void TileManager::addAerodromSource(const QString &icao)
{
    bool replaced = false;
    if (TileProvider *old = m_aerodroms.take(icao)) {
        m_scheduler->removeSource(old);
        m_store.removeQuality(mapGL::TileQuality::Aerodrom);
        invalidateTiles();
        replaced = true;
    }

    TileProvider *provider = new AerodromTileRasterProvider(icao, mapGL::aeroDB(icao));
    m_aerodroms.insert(icao, provider);
    m_scheduler->addSource(provider);

    if (replaced && !m_lastKeys.isEmpty()) {
        m_scheduler->setVisibleTiles(m_lastKeys);
    }
}

void TileManager::releaseLoader(const QString &icao)
{
    const Loader l = m_loaders.take(icao);
    if (!l.thread) {
        return;
    }
    disconnect(l.loader, nullptr, this, nullptr);
    connect(l.thread, &QThread::finished, l.thread, &QObject::deleteLater);
    l.thread->quit();
}

void TileManager::onAerodromTilesReady(const QString &icao)
{
    releaseLoader(icao);
    addAerodromSource(icao);
    emit aerodromReady(icao);
}

void TileManager::onAerodromTilesFailed(const QString &icao, int status, const QString &msg)
{
    releaseLoader(icao);
    mapGL::warning(QStringLiteral("aerodrom %1: HTTP %2: %3").arg(icao).arg(status).arg(msg));
    emit aerodromFailed(icao, msg);
}

void TileManager::onCameraChanged()
{
    update();

    if (!m_reloadPending) {
        m_reloadPending = true;
        QMetaObject::invokeMethod(this, [this]() { reloadTiles(); }, Qt::QueuedConnection);
    }
}

void TileManager::reloadTiles()
{
    m_reloadPending = false;
    if (!m_camera) return;

    QVector<uint64_t> keys;
    QSet<uint64_t> seen;
    for (const VisibleTile &t : tilesToShow()) {
        const uint64_t key = mapGL::math::makeKey(t.z, t.x, t.y);
        if (!seen.contains(key)) {
            seen.insert(key);
            keys.push_back(key);
        }
    }

    if (keys == m_lastKeys) return;
    m_lastKeys = keys;

    m_scheduler->setVisibleTiles(keys);
}

bool TileManager::tileImage(uint64_t key, QImage *img, quint32 *version) const
{
    return m_store.get(key, img, version);
}
