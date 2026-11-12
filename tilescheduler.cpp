#include "tilescheduler.h"

#include <QThread>
#include <limits>

TileScheduler::TileScheduler(TileStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    connect(&m_decoder, &TileDecoder::decoded, this, &TileScheduler::onDecoded);
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

    src->setStore(m_store);

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
    connect(src, &TileProvider::readyTile, this, &TileScheduler::onRawTile);
    connect(src, &TileProvider::failedTile, this, &TileScheduler::onFailedTile);

    // init() — в потоке провайдера
    QMetaObject::invokeMethod(src, [src] { src->init(); }, Qt::QueuedConnection);

    m_source.push_back(s);

    // карта уже показывает что-то — новому источнику сразу даём работу
    if (!m_visible.isEmpty()) {
        src->setLatestGeneration(m_generation);
        requestLoad(s, m_visible, m_generation);
    }
}

void TileScheduler::removeSource(mapGL::TileQuality quality)
{
    for (int i = m_source.size() - 1; i >= 0; --i) {
        if (m_source[i].provider->quality() == quality) {
            destroySource(m_source[i]);
            m_source.remove(i);
        }
    }
}

void TileScheduler::setVisibleTiles(const QVector<uint64_t> &keys)
{
    m_visible = keys;
    const quint64 generation = ++m_generation;

    for (const Source &src : std::as_const(m_source)) {
        // атомарно и сразу: уже стоящие в очереди load() станут устаревшими
        src.provider->setLatestGeneration(generation);
        requestLoad(src, keys, generation);
    }
}

void TileScheduler::requestLoad(const Source &src, const QVector<uint64_t> &keys, quint64 generation)
{
    TileProvider *p = src.provider;
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

void TileScheduler::onRawTile(const mapGL::TileMeta &tile)
{
    const mapGL::TileQuality q = tile.rwTile.quality;

    // пока байты шли сюда, мог успеть прийти тайл не хуже
    if (m_store->has(tile.key, q)) {
        return;
    }
    const auto it = m_decoding.constFind(tile.key);
    if (it != m_decoding.cend() && it.value() >= q) {
        return;
    }

    m_decoding.insert(tile.key, q);
    m_decoder.decode(tile);
}

void TileScheduler::onDecoded(uint64_t key, const QImage &img, mapGL::TileQuality quality)
{
    const auto it = m_decoding.find(key);
    if (it != m_decoding.end() && it.value() == quality) {
        m_decoding.erase(it);
    }

    if (img.isNull()) {
        onFailedTile(key, QStringLiteral("не удалось декодировать картинку"));
        return;
    }

    if (m_store->insert(key, img, quality)) {
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
