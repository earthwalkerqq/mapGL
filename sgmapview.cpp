#include "sgmapview.h"
#include "mapGL.h"

#include <QList>
#include <QQuickWindow>
#include <QRunnable>
#include <QSGNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <cmath>
#include <utility>

namespace {

// удалить текстуры в потоке рендера
class TextureCleanupJob : public QRunnable
{
public:
    explicit TextureCleanupJob(QList<QSGTexture *> textures) : m_textures(std::move(textures)) {}
    void run() override { qDeleteAll(m_textures); }

private:
    QList<QSGTexture *> m_textures;
};

} // namespace

SGMapView::SGMapView(QQuickItem *parent)
    : QQuickItem(parent)
{
    // собственное графическое содержимое для отрисовки
    setFlag(ItemHasContents, true);
}

void SGMapView::itemChange(ItemChange change, const ItemChangeData &value)
{
    if (change == ItemSceneChange) {
        disconnect(m_invalidatedConn);
        if (value.window) {
            // сигнал приходит в потоке рендера — обрабатываем там же
            m_invalidatedConn = connect(value.window, &QQuickWindow::sceneGraphInvalidated,
                                        this, [this] { invalidateSceneGraph(); },
                                        Qt::DirectConnection);
        }
    }
    QQuickItem::itemChange(change, value);
}

void SGMapView::dropTextures()
{
    for (const CachedTexture &c : std::as_const(m_textures))
        delete c.texture;
    m_textures.clear();
    m_textureBytes = 0;
}


void SGMapView::invalidateSceneGraph()
{
    dropTextures();
    m_nodes.clear();
    m_tileRoot = nullptr;
    m_backgroundNode = nullptr;
}

void SGMapView::releaseResources()
{
    QList<QSGTexture *> textures;
    const QHash<uint64_t, CachedTexture> &cached = m_textures;
    for (const CachedTexture &c : cached)
        textures.append(c.texture);
    m_textures.clear();
    m_textureBytes = 0;
    m_nodes.clear();
    m_tileRoot = nullptr;
    m_backgroundNode = nullptr;
    if (!textures.isEmpty() && window())
        window()->scheduleRenderJob(new TextureCleanupJob(std::move(textures)),
                                    QQuickWindow::BeforeSynchronizingStage);
}

QSGTexture *SGMapView::textureFor(uint64_t key)
{
    quint32 version = 0;
    const bool inStore = tileImage(key, nullptr, &version);
    const bool budgetLeft = m_uploadsThisFrame < kMaxUploadsPerFrame;

    const auto it = m_textures.find(key);
    if (it != m_textures.end()) {
        /*
         * Текстура актуальна — отдаём. Если картинку уже вытеснили из ОЗУ
         * (!inStore) — тоже отдаём, что есть. Если пришла новая версия, а
         * лимит загрузок исчерпан — пока показываем старую.
         */
        const bool outdated = inStore && it->version != version;
        if (!outdated || !budgetLeft) {
            if (outdated)
                m_needMoreUploads = true;
            it->lastFrame = m_frame;
            return it->texture;
        }
    }

    if (!inStore || !window())
        return nullptr;
    if (!budgetLeft) {
        m_needMoreUploads = true;   // покажем предка, догрузим в следующем кадре
        return nullptr;
    }

    QImage image;
    if (!tileImage(key, &image, &version) || image.isNull())
        return nullptr;
    const QImage *img = &image;

    /*
     * Без TextureHasAlphaChannel текстура считается непрозрачной и рисуется без
     * смешивания — так быстрее, и для подложки это верно. Но у картинки аэродрома
     * вне полос всё прозрачное: без флага там будет чёрный цвет поверх подложки.
     */
    const QQuickWindow::CreateTextureOptions opts = img->hasAlphaChannel()
                                                        ? QQuickWindow::TextureHasAlphaChannel
                                                        : QQuickWindow::CreateTextureOptions();
    QSGTexture *tex = window()->createTextureFromImage(*img, opts);
    if (!tex)
        return nullptr;
    tex->setFiltering(QSGTexture::Linear);
    ++m_uploadsThisFrame;
    const qint64 bytes = qint64(img->width()) * img->height() * 4;

    if (it != m_textures.end()) {
        /*
         * Старая версия: удаляем сразу. Безопасно — мы в sync-фазе, кадр сейчас
         * не рисуется, а все узлы ниже в этом же кадре получат текстуры заново.
         * В этом кадре старую текстуру никто не получил: версии в хранилище
         * во время sync не меняются (главный поток стоит), значит при более
         * раннем обращении она тоже была бы признана устаревшей.
         */
        delete it->texture;
        m_textureBytes -= it->bytes;
        *it = CachedTexture{tex, m_frame, bytes, version};
    } else {
        m_textures.insert(key, CachedTexture{tex, m_frame, bytes, version});
    }
    m_textureBytes += bytes;
    return tex;
}

void SGMapView::evictTextures()
{
    if (m_textureBytes <= kMaxTextureBytes)
        return;
    // кандидаты — не показанные в этом кадре, самые старые первыми
    std::vector<std::pair<quint64, uint64_t>> old;
    for (auto it = m_textures.cbegin(); it != m_textures.cend(); ++it) {
        if (it->lastFrame != m_frame)
            old.emplace_back(it->lastFrame, it.key());
    }
    std::sort(old.begin(), old.end());
    for (const auto &o : old) {
        if (m_textureBytes <= kMaxTextureBytes)
            break;
        const CachedTexture c = m_textures.value(o.second);
        delete c.texture;
        m_textureBytes -= c.bytes;
        m_textures.remove(o.second);
    }
}

QSGNode *SGMapView::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QSGNode *root = oldNode;
    // первый кадр или граф сцены пересоздан
    if (!root) {
        root = new QSGNode();
        m_nodes.clear();
        m_backgroundNode = nullptr;
        m_tileRoot = new QSGNode();
        root->appendChildNode(m_tileRoot);
    }

    // данные под теми же ключами поменялись — старые текстуры не годятся.
    // Узлы ниже в этом же кадре получат новые текстуры, до отрисовки старые не дойдут.
    if (m_dropTextures) {
        dropTextures();
        m_dropTextures = false;
    }

    // фон — первым ребёнком, чтобы тайлы были поверх
    if (m_background.alpha() > 0) {
        if (!m_backgroundNode) {
            m_backgroundNode = new QSGSimpleRectNode;
            root->prependChildNode(m_backgroundNode);
        }
        m_backgroundNode->setRect(boundingRect());
        m_backgroundNode->setColor(m_background);
    } else if (m_backgroundNode) {
        root->removeChildNode(m_backgroundNode);
        delete m_backgroundNode;
        m_backgroundNode = nullptr;
    }

    ++m_frame;
    m_uploadsThisFrame = 0;
    m_needMoreUploads = false;

    // что показать: для каждого видимого тайла — своя текстура или часть текстуры предка
    struct Piece
    {
        QRectF target;      // на экране
        QSGTexture *texture;
        QRectF source;      // в пикселях текстуры
    };
    std::vector<Piece> pieces;
    if (m_camera) {
        for (const VisibleTile &t : tilesToShow()) {
            // края по целым пикселям: соседние тайлы стыкуются без щелей
            const double x0 = std::floor(t.screenX), y0 = std::floor(t.screenY);
            const QRectF target(x0, y0, std::floor(t.screenX + t.tileSize) - x0,
                                std::floor(t.screenY + t.tileSize) - y0);
            for (int up = 0; up <= t.z; ++up) {
                const uint32_t px = t.x >> up, py = t.y >> up;   // предок на уровне t.z − up
                QSGTexture *tex = textureFor(mapGL::math::makeKey(t.z - up, px, py));
                if (!tex)
                    continue;
                // предок в 2^up раз крупнее: берём из него квадрат стороной size / 2^up
                const double part = double(tex->textureSize().width()) / double(1u << up);
                pieces.push_back({target, tex,
                                  QRectF(double(t.x - (px << up)) * part,
                                         double(t.y - (py << up)) * part, part, part)});
                break;
            }
        }
    }

    /*
     * связываем pieces с графическими объектами m_nodes на gpu
     * Главная идея не удалять всю сцену, а переиспользовать ее
     * Создание и удаление объектов в цикле отрисовки - дорого
     */

    // тайлов на экране стало меньше тайлов на gpu
    // вытащим лишние тайлы и удалим
    while (m_nodes.size() > pieces.size()) {
        QSGSimpleTextureNode *n = m_nodes.back();
        m_nodes.pop_back();
        m_tileRoot->removeChildNode(n);
        delete n;
    }
    // тайлов на экране стало больше
    // создаем новые заготовки под тектуры на gpu
    while (m_nodes.size() < pieces.size()) {
        auto *n = new QSGSimpleTextureNode;
        n->setFiltering(QSGTexture::Linear);
        /*
         * текстура лежит в кэше и может использоваться
         * в другом месте, поэтому при удалении узла
         * не должно происходить удаление текстуры.
         * Фиксится флагом setOwnsTexture(false), чтобы
         * узел воспринимался как "рамка" без владения текстурой
         */
        n->setOwnsTexture(false);          // текстурами владеет кэш слоя
        m_tileRoot->appendChildNode(n);
        m_nodes.push_back(n);
    }
    // накатим текстуры на узлы gpu
    for (size_t i = 0; i < pieces.size(); ++i) {
        QSGSimpleTextureNode *n = m_nodes[i];
        n->setTexture(pieces[i].texture);
        n->setRect(pieces[i].target);
        n->setSourceRect(pieces[i].source);
    }

    // Очистить старые текстуры (которые не используются)
    evictTextures();

    /*
     * Не всё успели загрузить в видеопамять — попросить следующий кадр.
     * update() — метод главного потока, поэтому через очередь событий
     * (мы сейчас в потоке рендера).
     */
    if (m_needMoreUploads)
        QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);

    return root;
}

void SGMapView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (m_camera && newGeometry.size() != oldGeometry.size())
        m_camera->setViewport(newGeometry.width(), newGeometry.height());
}
