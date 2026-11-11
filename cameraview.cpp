#include "cameraview.h"

#include <algorithm>
#include <cmath>

    /*
 * Центр камеры хранится в нормализованных координатах: весь мир —
 * квадрат [0; 1] x [0; 1]. Пиксели появляются только при переводе
 * на экран, поэтому при смене зума центр пересчитывать не нужно.
 *
 *   worldSize(zoom) = 256 · 2^zoom        — мир в пикселях экрана
 *   мир -> экран:  sx = (nx − cx) · worldSize + w/2
 *   экран -> мир:  nx = cx + (sx − w/2) / worldSize
 */

    static inline double worldSize(const double zoom)
{
    return kTileSize * std::pow(2.0, zoom);
}

void CameraView::normalizeCenter()
{
    m_cx -= std::floor(m_cx);                    // [0; 1): мир по долготе замкнут
    m_cy = qBound(0.0, m_cy, 1.0);           // за полюс не уходим
}

void CameraView::setCenterLatLon(double lat, double lon)
{
    const double phi = lat * M_PI / 180.0; // широта в радианах
    m_cx = (lon + 180.0) / 360.0;
    m_cy = (1.0 - std::log(std::tan(M_PI / 4 + phi / 2)) / M_PI) / 2.0;
    normalizeCenter();
    emit cameraChanged();
}

double CameraView::centerLat() const
{
    return std::atan(std::sinh(M_PI * (1.0 - 2.0 * m_cy))) * 180.0 / M_PI;
}

double CameraView::centerLon() const
{
    return m_cx * 360.0 - 180.0;
}

void CameraView::setZoom(double z)
{
    // центр в нормализованных координатах от зума не зависит — пересчитывать не нужно
    m_zoom = qBound(kMinZoom, z, kMaxZoom);
    emit cameraChanged();
}

void CameraView::panBy(double dx, double dy)
{
    // dx, dy — пиксели экрана; переводим в доли мира на текущем зуме
    const double ws = worldSize(m_zoom);
    m_cx -= dx / ws;
    m_cy -= dy / ws;
    normalizeCenter();
    emit cameraChanged();
}

void CameraView::zoomAround(double newZoom, double sx, double sy)
{
    const double before = worldSize(m_zoom);
    const double nx = m_cx + (sx - m_w / 2) / before;
    const double ny = m_cy + (sy - m_h / 2) / before;

    m_zoom = qBound(kMinZoom, newZoom, kMaxZoom);

    const double after = worldSize(m_zoom);
    m_cx = nx - (sx - m_w / 2) / after;
    m_cy = ny - (sy - m_h / 2) / after;
    normalizeCenter();
    emit cameraChanged();
}

QTransform CameraView::worldToScreen() const
{
    // sx = nx · worldSize − left,  left = cx · worldSize − w/2
    const double ws = worldSize(m_zoom);
    return QTransform(ws, 0, 0, ws, m_w / 2 - m_cx * ws, m_h / 2 - m_cy * ws);
}

std::vector<VisibleTile> CameraView::visibleTiles(int minTileZoom, int maxTileZoom) const
{
    std::vector<VisibleTile> out;
    if (int(std::lround(m_zoom)) < minTileZoom)
        return out;                  // мельче, чем есть данные у источника
    const int tz = tileZoom(minTileZoom, maxTileZoom);
    const int n = 0x01 << tz;
    const double ws = worldSize(m_zoom);
    const double size = kTileSize * std::pow(2.0, m_zoom - tz); // тайл в пикселях

    // левый/верхний края экрана в пикселях мира
    const double left = m_cx * ws - m_w / 2;
    const double top  = m_cy * ws - m_h / 2;
    // индексы начальных/конечных тайлов в экране (делим на размер тайла на экране)
    const int x0 = int(std::floor(left / size));
    const int x1 = int(std::floor((left + m_w) / size));
    const int y0 = std::max(0, int(std::floor(top / size)));
    const int y1 = std::min(n - 1, int(std::floor((top + m_h) / size)));
    // центр камеры в единицах тайлов уровня tz
    const double ctx = m_cx * n, cty = m_cy * n;

    for (int ty = y0; ty <= y1; ++ty) {
        for (int tx = x0; tx <= x1; ++tx) {
            VisibleTile t;
            t.z = tz;
            t.tileSize = size;
            // адрес
            t.x = uint32_t(((tx % n) + n) % n);
            t.y = uint32_t(ty);
            // позиция
            t.screenX = tx * size - left;
            t.screenY = ty * size - top;
            const double dx = tx + 0.5 - ctx, dy = ty + 0.5 - cty;
            t.InvertPriority = dx * dx + dy * dy;
            out.push_back(t);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const VisibleTile &a, const VisibleTile &b) {
                  return a.InvertPriority < b.InvertPriority;
              });
    return out;
}
