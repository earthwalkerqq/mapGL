#include "tilescheduler.h"

#include <QThread>
#include <limits>

TileScheduler::TileScheduler(TileStoreRaster *storeRaster, TileStoreVector *storeVector, QObject *parent)
    : QObject(parent), m_storeRaster(storeRaster), m_storeVector(storeVector)
{
    connect(&m_decoder, &TileDecoder::decodedRaster, this, &TileScheduler::onDecodedRaster);
    connect(&m_decoder, &TileDecoder::decodedVector, this, &TileScheduler::onDecodedVector);
}

TileScheduler::~TileScheduler()
{
    // сначала останавливаем провайдеры (они читают m_store), потом пул декодера
    for (const Source &src : std::as_const(m_source)) {
        destroySource(src);
    }
    m_source.clear();
}

void TileScheduler::addSource(TileProvider *src)
{
    /*
     * провайдер не должен быть связан с объектами
     * других потоков
     */
    Q_ASSERT(src && !src->parent());

    src->setStoreRaster(m_storeRaster);
    src->setStoreVector(m_storeVector);

    Source s;
    s.provider = src;
    if (src->needsOwnThread()) {
        s.thread = new QThread;
        s.thread->setObjectName(QStringLiteral("TileProvider-%1").arg(int(src->quality())));
        src->moveToThread(s.thread);
        connect(s.thread, &QThread::finished, src, &QObject::deleteLater);
        s.thread->start();
    } else {
        // если отдельный поток не нужен - берем во владение
        src->setParent(this);
    }

    // провайдер в другом потоке -> соединения автоматически queued
    connect(src, &TileProvider::readyTileRaster, this, &TileScheduler::onRawTileRaster);
    connect(src, &TileProvider::readyTileVector, this, &TileScheduler::onRawTileVector);
    connect(src, &TileProvider::failedTile, this, &TileScheduler::onFailedTile);

    // init() — в потоке провайдера
    QMetaObject::invokeMethod(src, [src] { src->init(); }, Qt::QueuedConnection);

    m_source.push_back(s);

    // карта уже показывает что-то — новому источнику сразу даём работу
    if (!m_visibleRaster.isEmpty() || !m_visibleVector.isEmpty()) {
        src->setLatestGeneration(m_generation);
        requestLoad(s, m_generation);
    }
}

void TileScheduler::removeSource(mapGL::TileQuality quality)
{
    for (int i = m_source.size() - 1; i >= 0; --i) {
        if (m_source[i].provider->quality() == quality) {
            dropDecoding(quality, m_source[i].provider->isVector());
            destroySource(m_source[i]);
            m_source.remove(i);
        }
    }
}

void TileScheduler::removeSource(TileProvider *src)
{
    for (int i = m_source.size() - 1; i >= 0; --i) {
        if (m_source[i].provider == src) {
            dropDecoding(src->quality(), src->isVector());
            destroySource(m_source[i]);
            m_source.remove(i);
            return;
        }
    }
}

void TileScheduler::setVisibleTiles(const QVector<uint64_t> &rasterKeys, const QVector<uint64_t> &vectorKeys)
{
    m_visibleRaster = rasterKeys;
    m_visibleVector = vectorKeys;
    const quint64 generation = ++m_generation;

    for (const Source &src : std::as_const(m_source)) {
        // атомарно и сразу: уже стоящие в очереди load() станут устаревшими
        src.provider->setLatestGeneration(generation);
        requestLoad(src, generation);
    }
}

void TileScheduler::requestLoad(const Source &src, quint64 generation)
{
    TileProvider *p = src.provider;
    const QVector<uint64_t> keys = p->isVector() ? m_visibleVector : m_visibleRaster;
    if (keys.isEmpty()) {
        return;
    }
    // всегда через очередь событий провайдера: в его потоке и без рекурсии в главном
    QMetaObject::invokeMethod(p, [p, keys, generation] { p->load(keys, generation); },
                              Qt::QueuedConnection);
}

void TileScheduler::destroySource(const Source &src)
{
    TileProvider *p = src.provider;
    disconnect(p, nullptr, this, nullptr);
    p->setLatestGeneration(std::numeric_limits<quint64>::max());

    if (src.thread) {
        // finished -> deleteLater
        src.thread->quit();
        src.thread->wait();
        delete src.thread;
    } else {
        delete p;
    }
}

void TileScheduler::onRawTileRaster(const mapGL::TileMeta &tile)
{
    const mapGL::TileQuality q = tile.rwTile.quality;

    // пока байты шли сюда, мог успеть прийти тайл не хуже
    if (m_storeRaster->has(tile.key, q)) {
        return;
    }
    const auto it = m_decodingRaster.constFind(tile.key);
    if (it != m_decodingRaster.cend() && it.value() >= q) {
        return;
    }

    m_decodingRaster.insert(tile.key, q);
    m_decoder.decodeRaster(tile);
}

void TileScheduler::onRawTileVector(const mapGL::TileMeta &tile)
{
    const mapGL::TileQuality q = tile.rwTile.quality;

    // пока байты шли сюда, мог успеть прийти тайл не хуже
    if (m_storeVector->has(tile.key, q)) {
        return;
    }
    const auto it = m_decodingVector.constFind(tile.key);
    if (it != m_decodingVector.cend() && it.value() >= q) {
        return;
    }

    m_decodingVector.insert(tile.key, q);
    m_decoder.decodeVector(tile);
}

void TileScheduler::dropDecoding(mapGL::TileQuality quality, bool vector)
{
    QHash<uint64_t, mapGL::TileQuality> &decoding = vector ? m_decodingVector : m_decodingRaster;
    for (auto it = decoding.begin(); it != decoding.end();) {
        it = (it.value() == quality) ? decoding.erase(it) : std::next(it);
    }
}

void TileScheduler::onDecodedRaster(uint64_t key, const QImage &img, mapGL::TileQuality quality)
{
    const auto it = m_decodingRaster.find(key);
    if (it == m_decodingRaster.end() || it.value() != quality) {
        return;
    }
    m_decodingRaster.erase(it);

    if (img.isNull()) {
        onFailedTile(key, QStringLiteral("не удалось декодировать картинку"));
        return;
    }

    if (m_storeRaster->insert(key, img, quality)) {
        emit tilesChanged();
    }
}

void TileScheduler::onDecodedVector(uint64_t key, const QImage &img, mapGL::TileQuality quality)
{
    const auto it = m_decodingVector.find(key);
    if (it == m_decodingVector.end() || it.value() != quality) {
        return;
    }
    m_decodingVector.erase(it);

    if (img.isNull()) {
        onFailedTile(key, QStringLiteral("не удалось растеризовать векторный тайл"));
        return;
    }

    if (m_storeVector->insert(key, img, quality)) {
        emit tilesChanged();
    }
}

void TileScheduler::onFailedTile(uint64_t key, const QString &msg)
{
    uint8_t z;
    uint32_t x, y;
    mapGL::math::parseKey(key, &z, &x, &y);
    mapGL::warning(QStringLiteral("Error for key %1:%2:%3 - %4")
        .arg(z).arg(x).arg(y).arg(msg));
}
