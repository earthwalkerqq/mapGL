#ifndef TILEMANAGER_H
#define TILEMANAGER_H

/*
 * связывает камеру, планировщик загрузки и рисование.
 */

#include "sgmapview.h"
#include "tilestore.h"
#include "tilescheduler.h"

#include <QHash>
#include <QString>
#include <memory>

class QThread;
class TileLoader;

class TileManager : public SGMapView
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit TileManager(QQuickItem *parent = nullptr);
    ~TileManager() override;

    bool openAerodrom(const QString &icao);
    bool downloadAerodrom(const QString &icao);
    bool isAerodromOpened(const QString &icao) const;
    bool isAerodromLoading(const QString &icao) const;

signals:
    void aerodromReady(const QString &icao);
    void aerodromFailed(const QString &icao, const QString &msg);

protected:
    void onCameraChanged() override;
    bool tileImage(uint64_t key, QImage *img, quint32 *version) const override;

private:
    struct Loader
    {
        TileLoader *loader = nullptr;
        QThread *thread = nullptr;
    };

    // пересчитать набор нужных тайлов
    void reloadTiles();

    void addAerodromSource(const QString &icao);
    void onAerodromTilesReady(const QString &icao);
    void onAerodromTilesFailed(const QString &icao, int status, const QString &msg);
    void releaseLoader(const QString &icao);
    static QString normalizeIcao(const QString &icao);

    // планировщик позже хранилища - удалиться раньше
    TileStore m_store;
    std::unique_ptr<TileScheduler> m_scheduler;

    QHash<QString, TileProvider *> m_aerodroms;
    QHash<QString, Loader> m_loaders;

    QVector<uint64_t> m_lastKeys;
    bool m_reloadPending = false;
};

#endif // TILEMANAGER_H
