#include "tilemanager.h"

#include "aerodromtilerasterprovider.h"
#include "aerodromtilevectorprovider.h"
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
    , m_storeVec(static_cast<qsizetype>(mapGL::convertToBytes(32., mapGL::DataUnit::MB)))
    , m_scheduler(std::make_unique<TileScheduler>(&m_store, &m_storeVec))
{
    m_scheduler->addSource(new FallbackTileProvider());
    m_scheduler->addSource(new NetworkTileProvider());
    connect(m_scheduler.get(), &TileScheduler::tilesChanged, this, [this]() { update(); });
    connect(m_scheduler.get(), &TileScheduler::proceduresReady, this, &TileManager::onProceduresReady);
    connect(m_scheduler.get(), &TileScheduler::proceduresFailed, this, &TileManager::proceduresFailed);

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
    addAerodromVectorSource(code);

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

void TileManager::addAerodromVectorSource(const QString &code)
{
    if (m_vectorAerodroms.contains(code)) {
        return;
    }
    m_vectorAerodroms.append(code);

    if (m_vectorProvider) {
        m_scheduler->removeSource(m_vectorProvider);
        m_storeVec.removeQuality(mapGL::TileQuality::AerodromVector);
        invalidateTiles();
    }

    m_vectorProvider = new AerodromTileVectorProvider(m_vectorAerodroms);
    m_scheduler->addSource(m_vectorProvider);
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

    TileProvider *provider = new AerodromTileRasterProvider(icao);
    m_aerodroms.insert(icao, provider);
    m_scheduler->addSource(provider);

    if (replaced && !m_lastKeys.isEmpty()) {
        m_scheduler->setVisibleTiles(m_lastKeys, m_lastVectorKeys);
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
    addAerodromVectorSource(icao);
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

    const QVector<uint64_t> keys = uniqueKeys(tilesToShow(RasterLayer));
    const QVector<uint64_t> vectorKeys = uniqueKeys(tilesToShow(VectorLayer));

    if (keys == m_lastKeys && vectorKeys == m_lastVectorKeys) return;
    m_lastKeys = keys;
    m_lastVectorKeys = vectorKeys;

    m_scheduler->setVisibleTiles(keys, vectorKeys);
}

QVector<uint64_t> TileManager::uniqueKeys(const QVector<VisibleTile> &tiles)
{
    QVector<uint64_t> keys;
    QSet<uint64_t> seen;
    for (const VisibleTile &t : tiles) {
        const uint64_t key = mapGL::math::makeKey(t.z, t.x, t.y);
        if (!seen.contains(key)) {
            seen.insert(key);
            keys.push_back(key);
        }
    }
    return keys;
}

QVector<VisibleTile> TileManager::tilesToShow(int layer) const
{
    if (layer == VectorLayer) {
        return m_camera->visibleTiles(k_minVectorZoom, mapGL::k_maxTileVectorZoom);
    }
    return m_camera->visibleTiles();
}

bool TileManager::tileImage(int layer, uint64_t key, QImage *img, quint32 *version) const
{
    if (layer == VectorLayer) {
        return m_storeVec.get(key, img, version);
    }
    return m_store.get(key, img, version);
}

void TileManager::loadProcedures(const QString &icao, const QString &runway)
{
    const QString code = normalizeIcao(icao);
    if (code.isEmpty()) {
        emit proceduresFailed(icao, runway, QStringLiteral("некорректный код аэродрома: %1").arg(icao));
        return;
    }
    m_scheduler->requestProcedures(code, runway);
}

void TileManager::onProceduresReady(const mapGL::proc::RunwayProcedures &schemes)
{
    m_procedures.setSchemes(schemes);
    update();
    emit proceduresReady(schemes.icao, schemes.runway.ident);
}

QStringList TileManager::procedureNames(int kind) const
{
    return validKind(kind) ? m_procedures.names(mapGL::proc::Kind(kind)) : QStringList();
}

QStringList TileManager::procedureTransitions(int kind, const QString &route) const
{
    return validKind(kind) ? m_procedures.transitions(mapGL::proc::Kind(kind), route) : QStringList();
}

bool TileManager::showProcedure(int kind, const QString &route, const QString &transition)
{
    if (!validKind(kind))
        return false;
    const bool found = m_procedures.show(mapGL::proc::Kind(kind),
                                         route.trimmed().toUpper(),
                                         transition.trimmed().toUpper());
    update();
    return found;
}

void TileManager::hideProcedure(int kind, const QString &route)
{
    if (!validKind(kind))
        return;
    m_procedures.hide(mapGL::proc::Kind(kind), route.trimmed().toUpper());
    update();
}

void TileManager::hideAllProcedures(void)
{
    m_procedures.hideAll();
    update();
}

void TileManager::updateOverlay(QSGNode *overlayRoot)
{
    if (!m_camera)
        return;
    m_procedures.updateNodes(overlayRoot, window(), m_camera->worldToScreen(),
                             m_camera->centerWorldX());
}
