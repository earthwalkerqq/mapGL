#ifndef TILEPROVIDER_H
#define TILEPROVIDER_H

/*
 * Абстрактный источник тайлов.
 *
 * Провайдер умеет одно: по ключам достать СЫРЫЕ байты и отдать их сигналом
 * readyTileRaster() или readyTileVector(). Он не декодирует (это TileDecoder), не хранит готовые
 * картинки (это TileStore) и не знает про камеру (ключи ему передаёт
 * TileScheduler).
 *
 * Потоки. Провайдер живёт либо в своём потоке (needsOwnThread() == true,
 * например SQLite — блокирующие вызовы), либо в главном (сеть: QNetworkAccessManager
 * и так асинхронный). Всё, что нужно потоку, создаётся в init() — он
 * вызывается уже в потоке провайдера.
 *
 * Поколение (generation). Каждый новый набор видимых тайлов получает номер.
 * Планировщик сразу (атомарно, из главного потока) сообщает провайдеру
 * последний номер, а задания load() приходят в очередь. Если камера
 * успела уехать, устаревший load() увидит isStale() и выйдет — очередь
 * не копится, работа на ненужные тайлы не тратится.
 */

#include "mapGL.h"
#include "tilestore.h"

#include <QObject>
#include <QVector>
#include <atomic>

class TileProvider : public QObject
{
    Q_OBJECT

public:
    explicit TileProvider(QObject *parent = nullptr) : QObject(parent) {}
    ~TileProvider() override = default;

    virtual mapGL::TileQuality quality(void) const = 0;
    // нужен ли провайдеру собственный поток (блокирующий ввод-вывод)
    virtual bool needsOwnThread(void) const { return false; }
    virtual bool isVector(void) const { return false; }

    // вызывается один раз, уже в потоке провайдера
    virtual void init(void) {}
    /*
     * keys — видимые тайлы, отсортированы по приоритету (ближе к центру — раньше),
     * без повторов. Вызывается в потоке провайдера.
     */
    virtual void load(const QVector<uint64_t> &keys, quint64 generation) = 0;

    // ---- вызываются из главного потока (TileScheduler) ----
    void setStoreRaster(const TileStoreRaster *store) { m_storeRaster = store; }
    void setStoreVector(const TileStoreVector *store) { m_storeVector = store; }
    void setLatestGeneration(quint64 generation)
    {
        m_latestGeneration.store(generation, std::memory_order_relaxed);
    }

signals:
    void readyTileVector(const mapGL::TileMeta &tile);
    void readyTileRaster(const mapGL::TileMeta &tile);
    void failedTile(uint64_t key, const QString &msgError);

protected:
    // камера уже уехала
    bool isStale(quint64 generation) const
    {
        return generation != m_latestGeneration.load(std::memory_order_relaxed);
    }
    // тайл такого же или лучшего качества уже готов — повторно не грузим
    bool alreadyHaveRaster(uint64_t key) const
    {
        return m_storeRaster && m_storeRaster->has(key, quality());
    }

    bool alreadyHaveVector(uint64_t key) const
    {
        return m_storeVector && m_storeVector->has(key, quality());
    }

private:
    const TileStoreRaster *m_storeRaster = nullptr;
    const TileStoreVector *m_storeVector = nullptr;
    std::atomic<quint64> m_latestGeneration{0};
};

#endif // TILEPROVIDER_H
