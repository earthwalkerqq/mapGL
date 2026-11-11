#ifndef CAMERAVIEW_H
#define CAMERAVIEW_H

/*
 * Класс для определения положения камеры
 * видимые тайлы подкачиваются в зависимости
 * от приоритета, расчитанного как квадрат
 * расстояния от центра тайла до центра камеры,
 * поэтому, чем меньше, тем приоритет выше
 *
 * Камера одна на карту: её создаёт MapView, а слои только читают её
 * и перерисовываются по сигналу cameraChanged().
 */

#include <QObject>
#include <QTransform>
#include <QtQml/QQmlEngine>
#include <cmath>
#include <cstdint>
#include <vector>

// constexpr, а не inline constexpr: inline-переменные — C++17, а собираем и старым компилятором
constexpr double kTileSize = 256.0;
constexpr double kMinZoom = 0.0;
constexpr double kMaxZoom = 18.0;
constexpr int kMaxTileZoom = 12;

struct VisibleTile
{
    int z;
    uint32_t x, y;
    double tileSize;         // сторона тайла на экране, px: 256*2^(zoom − z)
    double screenX, screenY; // левая/верхняя стороны тайла
    double InvertPriority;
};

class CameraView : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Объект CameraView находится в MapView, доступ осуществляется через map.camera")
    Q_PROPERTY(double zoom READ getZoom NOTIFY cameraChanged)
public:
    explicit CameraView(QObject *parent = nullptr) : QObject(parent) {}

    inline void setViewport(double w, double h)
    {
        m_w = w; m_h = h;
        emit cameraChanged();
    }
    /*
     * @brief
     * Получение центра экрана по долготе и широте
     * по проекции Web Mercator
     */
    void setCenterLatLon(double lat, double lon);
    void setZoom(double z);
    /*
     * @brief сдвиг камеры в пикселях экрана
     * @details по широте есть ограничения на полюсах
     * а по долготе мир замнут [0; 1)
     */
    void panBy(double dx, double dy);
    double getZoom() const { return m_zoom; }
    // центр камеры в градусах (обратная проекция Web Mercator)
    double centerLat() const;
    double centerLon() const;
    void zoomAround(double newZoom, double sx, double sy);
    // вектор отсортированных по приоритету видимых тайлов
    QVector<VisibleTile> visibleTiles() const { return visibleTiles(0, kMaxTileZoom); }
    /*
     * то же со своим диапазоном зумов тайлов (векторные аэродромы — z8..z16):
     * если зум камеры меньше minTileZoom — пусто, больше maxTileZoom — тайлы maxTileZoom
     */
    QVector<VisibleTile> visibleTiles(int minTileZoom, int maxTileZoom) const;
    // нормализованные координаты мира 0..1 -> пиксели экрана
    QTransform worldToScreen() const;
signals:
    void cameraChanged();
private:
    // уровень тайлов для текущего дробного зума
    inline int tileZoom(int minTileZoom, int maxTileZoom) const
    {
        return qBound(minTileZoom, int(std::lround(m_zoom)), maxTileZoom);
    }
    // привести центр в допустимые пределы: по долготе мир замкнут, по широте — нет
    void normalizeCenter();

    double m_zoom = 5;         // зум дробный, но тайлы с целым
    double m_cx = 0, m_cy = 0; // центр в нормализованных координатах
    double m_w = 0, m_h = 0;   // размеры экрана в пикселях
};
#endif // CAMERAVIEW_H
