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
 * Базовый класс тайлового слоя карты на sg
 *
 * Слой подписан на сигнал камеры cameraChanged()
 * и по нему перерисовывается.
 *
 * Каждая картинка тайла один раз загружается в видеокарту
 * текстурой (QSGTexture) и дальше живёт там, в кэше слоя
 *
 * Все что связано с QSG* безопасно трогать только в потоке
 * рендера
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
        if (m_camera) {
            connect(m_camera, &CameraView::cameraChanged, this, &SGMapView::onCameraChanged);
            m_camera->setViewport(width(), height());
        }
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
    // размер элемента поменялся — сообщить камере размер экрана
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

    // камера сдвинулась - по умолчанию перерисоваться
    virtual void onCameraChanged() { update(); }
    // какие тайлы показать; по умолчанию — растровые уровни камеры (z0..kMaxTileZoom)
    virtual QVector<VisibleTile> tilesToShow() const { return m_camera->visibleTiles(); }
    // картинка тайла по ключу; false — её ещё нет.
    virtual bool tileImage(uint64_t key, QImage *img, quint32 *version) const = 0;

    void setBackgroundColor(const QColor &c) { m_background = c; update(); }
    // картинки тайлов поменялись: на следующем кадре удалить все текстуры и создать заново
    void invalidateTiles() { m_dropTextures = true; update(); }

    QPointer<CameraView> m_camera;

private:
    /*
     * Сперва пытается найти ключ в кэше, а зетем
     * вызывает tileImage
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
        quint32 version = 0;            // версия картинки, из которой сделана текстура
    };

    /*
     * Лимит видеопамяти на кэш текстур слоя — по байтам, а не по штукам:
     * растровый тайл 256×256 — 256 КБ, тайл аэродрома 512×512 — 1 МБ.
     */
    static constexpr qint64 kMaxTextureBytes = 48LL * 1024 * 1024;
    /*
     * Сколько текстур загружать в видеопамять за один кадр. Если разом пришло
     * 40 тайлов, 40 загрузок в одном кадре дадут рывок; остальные догрузятся
     * в следующих кадрах, а пока на их месте — кусок предка.
     */
    static constexpr int kMaxUploadsPerFrame = 8;

    // всё ниже — только из потока рендера
    QHash<uint64_t, CachedTexture> m_textures;
    qint64 m_textureBytes = 0;
    QSGNode *m_tileRoot = nullptr;      // контейнер узлов тайлов
    std::vector<QSGSimpleTextureNode *> m_nodes;
    QSGSimpleRectNode *m_backgroundNode = nullptr;
    // текущий кадр, на нем строится lru - самый старый кадр удалиться первым
    quint64 m_frame = 0;
    int m_uploadsThisFrame = 0;
    bool m_needMoreUploads = false;     // не всё загрузили — попросить ещё кадр

    QColor m_background = Qt::transparent;
    bool m_dropTextures = false;        // ставится в основном потоке, читается в sync (основной стоит)
    QMetaObject::Connection m_invalidatedConn;
};

#endif // MAPLAYER_H
