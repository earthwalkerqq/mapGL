#ifndef MAPLAYER_H
#define MAPLAYER_H

#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <vector>
#include <QSGSimpleRectNode>
#include "cameraview.h"

class QSGSimpleRectNode;
class QSGSimpleTextureNode;
class QSGTexture;

/*
 * Базовый класс тайлового слоя карты на графе сцены (scene graph).
 *
 * Слой только рисует себя по камере. Камеру он не создаёт и не двигает:
 * её создаёт MapView, он же обрабатывает мышь. Слой подписан на сигнал
 * камеры cameraChanged() и по нему перерисовывается.
 *
 * Как рисует. Каждая картинка тайла один раз загружается в видеокарту
 * текстурой (QSGTexture) и дальше живёт там, в кэше слоя. На каждый кадр
 * меняются только прямоугольники узлов: при перетаскивании ничего
 * не перерисовывается на процессоре и не загружается заново.
 *
 *   корневой узел
 *    ├─ QSGSimpleRectNode       фон (если задан)
 *    ├─ QSGNode                 тайлы:
 *    │   └─ QSGSimpleTextureNode × N  по одному на видимый тайл; если своей картинки
 *    │                                ещё нет — часть картинки предка (sourceRect)
 *    └─ (узлы наследника)       наследник может дописать свои узлы поверх тайлов
 *                               (подписи): вызвать MapLayer::updatePaintNode и
 *                               добавить детей в возвращённый корень
 *
 * Наследнику достаточно отдать картинку тайла по ключу — tileImage().
 * Если картинки под теми же ключами поменялись (другой аэродром) — invalidateTiles().
 *
 * Потоки. updatePaintNode() вызывается в потоке рендера, пока основной поток
 * остановлен: читать данные слоя и менеджера там безопасно, а всё, что связано
 * с QSG*, трогать можно только там.
 */
class SGMapView : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SGMapView — абстрактный базовый класс")
    Q_PROPERTY(CameraView *camera READ camera WRITE setCamera NOTIFY cameraObjectChanged)
public:
    explicit SGMapView(QQuickItem *parent = nullptr);

    CameraView *camera() const { return m_camera; }
    void setCamera(CameraView *c)
    {
        if (m_camera == c) return;
        if (m_camera) disconnect(m_camera, nullptr, this, nullptr);
        m_camera = c;
        if (m_camera)
            connect(m_camera, &CameraView::cameraChanged, this, &SGMapView::onCameraChanged);
        emit cameraObjectChanged();
        onCameraChanged();
    }

signals:
    void cameraObjectChanged();

protected:
    /*
     * будет вызываться в наследнике и использовать для получения тайла,
     * определенный в этом самом наследнике, метод tileImage() по ключу
     */
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    // слой уходит из окна: текстуры удалить в потоке рендера
    void releaseResources() override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

    /*
     * @details
     * камера сдвинулась: по умолчанию перерисоваться,
     * для производных - запрос тайлов из сети/кэша/mbtiles
     */
    virtual void onCameraChanged() { update(); }
    // какие тайлы показать; по умолчанию — растровые уровни камеры (z0..kMaxTileZoom)
    virtual std::vector<VisibleTile> tilesToShow() const { return m_camera->visibleTiles(); }
    /*
     * картинка тайла по ключу или nullptr, если её ещё нет;
     * вызывается из updatePaintNode — в потоке рендера при остановленном основном
     */
    virtual const QImage *tileImage(uint64_t key) const = 0;

    void setBackgroundColor(const QColor &c) { m_background = c; update(); }
    // картинки тайлов поменялись: на следующем кадре удалить все текстуры и создать заново
    void invalidateTiles() { m_dropTextures = true; update(); }

    QPointer<CameraView> m_camera;

private:
    /*
     * @brief
     *  Нахождение текстуры для конкретного ключа
     * @details
     *  Сперва пытается найти ключ в кэше, а зетем
     *  вызывает tileImage
     */
    QSGTexture *textureFor(uint64_t key);
    // кэш текстур больше лимита — удалить давно не показанные
    void evictTextures();
    // граф сцены пересоздаётся (потеря контекста графики): всё наше уже недействительно
    void invalidateSceneGraph();

    /*
     * @brief
     *  удалить все текстуры кэша; только в потоке рендера
     * @details
     *  текстура связана с контектом рендера, поэтому удаление
     *  должно происходить в render thread
     */
    void dropTextures();

    struct CachedTexture
    {
        QSGTexture *texture = nullptr;
        quint64 lastFrame = 0;          // в каком кадре показывалась — для LRU
        qint64 bytes = 0;               // ширина × высота × 4
    };

    /*
     * Лимит видеопамяти на кэш текстур слоя — по байтам, а не по штукам:
     * растровый тайл 256×256 — 256 КБ, тайл аэродрома 512×512 — 1 МБ.
     */
    static constexpr qint64 kMaxTextureBytes = 48LL * 1024 * 1024;

    // всё ниже — только из потока рендера
    QHash<uint64_t, CachedTexture> m_textures;
    qint64 m_textureBytes = 0;
    QSGNode *m_tileRoot = nullptr;      // контейнер узлов тайлов
    std::vector<QSGSimpleTextureNode *> m_nodes;
    QSGSimpleRectNode *m_backgroundNode = nullptr;
    // текущий кадр, на нем строится lru - самый старый кадр удалиться первым
    quint64 m_frame = 0;

    QColor m_background = Qt::transparent;
    bool m_dropTextures = false;        // ставится в основном потоке, читается в sync (основной стоит)
    QMetaObject::Connection m_invalidatedConn;
};

#endif // MAPLAYER_H
