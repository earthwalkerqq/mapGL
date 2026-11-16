#include "tiledecoder.h"
#include "vectortile.h"

#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QThread>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

static constexpr int k_MaxThreads = 4;

namespace {

struct VectorStyle {
    const char *layer;
    QColor fill;
    QColor stroke;
    double strokePx;
    double defaultWidthM;
    double minWidthPx;
    double pointRadiusPx;
};

const VectorStyle k_vectorStyles[] = {
    {"aerodromes",        QColor(232, 226, 238, 150), QColor(150, 135, 175),      1.0,  0.0, 0.0, 0.0},
    {"aprons",            QColor(205, 202, 210, 235), QColor(170, 166, 176),      1.0,  0.0, 0.0, 0.0},
    {"taxiways",          QColor(),                   QColor(178, 173, 184),      0.0, 23.0, 1.0, 0.0},
    {"runways",           QColor(),                   QColor(70, 70, 80),         0.0, 45.0, 2.0, 0.0},
    {"stopways",          QColor(),                   QColor(120, 120, 130),      0.0, 45.0, 1.5, 0.0},
    {"terminals",         QColor(201, 169, 166, 235), QColor(158, 126, 123),      1.0,  0.0, 0.0, 2.5},
    {"helipads",          QColor(196, 213, 196, 235), QColor(110, 140, 110),      1.0,  0.0, 0.0, 3.0},
    {"parking_positions", QColor(),                   QColor(150, 156, 164),      1.0,  0.0, 0.0, 2.0},
    {"holding_positions", QColor(224, 60, 49),        QColor(224, 60, 49),        2.0,  0.0, 0.0, 2.5},
    {"gates",             QColor(92, 107, 192),       QColor(92, 107, 192),       1.0,  0.0, 0.0, 2.5},
};

double tileCenterLat(uint32_t y, uint8_t z)
{
    const double n = double(1u << z);
    return qRadiansToDegrees(std::atan(std::sinh(M_PI * (1.0 - 2.0 * (y + 0.5) / n))));
}

double parseWidthMeters(const QVariant &value, double fallback)
{
    if (!value.isValid())
        return fallback;

    QString text = value.toString().trimmed().toLower();
    double factor = 1.0;
    if (text.endsWith(QLatin1Char('\'')) || text.endsWith(QLatin1String("ft"))) {
        factor = 0.3048;
        text.remove(QLatin1Char('\'')).remove(QLatin1String("ft"));
    } else if (text.endsWith(QLatin1Char('m'))) {
        text.chop(1);
    }

    bool ok = false;
    const double v = text.trimmed().toDouble(&ok);
    return (ok && v > 0.) ? v * factor : fallback;
}

QPainterPath toPath(const QVector<QPolygonF> &parts, double scale, bool closed)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    for (const QPolygonF &part : parts) {
        if (part.isEmpty())
            continue;
        path.moveTo(part.first() * scale);
        for (qsizetype i = 1; i < part.size(); ++i)
            path.lineTo(part[i] * scale);
        if (closed)
            path.closeSubpath();
    }
    return path;
}

bool featureAllowed(const mapGL::mvt::Feature &feature, const mapGL::VectorFilter &filter,
                    double extent, uint8_t z, uint32_t x, uint32_t y)
{
    const QString code = feature.properties.value(QStringLiteral("icao")).toString().trimmed().toUpper();
    if (!code.isEmpty()) {
        for (const mapGL::AerodromZone &zone : filter.zones) {
            if (zone.icao == code)
                return true;
        }
        return false;
    }

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    for (const QPolygonF &part : feature.parts) {
        for (const QPointF &pt : part) {
            minX = std::min(minX, pt.x());
            minY = std::min(minY, pt.y());
            maxX = std::max(maxX, pt.x());
            maxY = std::max(maxY, pt.y());
        }
    }
    if (minX > maxX)
        return false;

    const double n = double(1u << z);
    const double left   = (x + minX / extent) / n;
    const double right  = (x + maxX / extent) / n;
    const double top    = (y + minY / extent) / n;
    const double bottom = (y + maxY / extent) / n;

    for (const mapGL::AerodromZone &zone : filter.zones) {
        if (right >= zone.area.left() && left <= zone.area.right()
            && bottom >= zone.area.top() && top <= zone.area.bottom())
            return true;
    }
    return false;
}

void drawFeature(QPainter &p, const VectorStyle &style, const mapGL::mvt::Feature &feature,
                 double scale, double metersPerPx)
{
    using mapGL::mvt::GeomType;

    switch (feature.type) {
    case GeomType::Polygon: {
        p.setBrush(style.fill.isValid() ? QBrush(style.fill) : Qt::NoBrush);
        p.setPen(style.strokePx > 0. ? QPen(style.stroke, style.strokePx) : Qt::NoPen);
        p.drawPath(toPath(feature.parts, scale, true));
        break;
    }
    case GeomType::LineString: {
        double widthPx = style.strokePx;
        if (style.defaultWidthM > 0.) {
            const double meters = parseWidthMeters(feature.properties.value(QStringLiteral("width")),
                                                   style.defaultWidthM);
            widthPx = qMax(style.minWidthPx, meters / metersPerPx);
        }
        if (widthPx <= 0.)
            return;
        QPen pen(style.stroke, widthPx);
        pen.setCapStyle(Qt::FlatCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(toPath(feature.parts, scale, false));
        break;
    }
    case GeomType::Point: {
        if (style.pointRadiusPx <= 0.)
            return;
        p.setPen(Qt::NoPen);
        p.setBrush(style.fill.isValid() ? style.fill : style.stroke);
        for (const QPolygonF &part : feature.parts) {
            for (const QPointF &pt : part)
                p.drawEllipse(pt * scale, style.pointRadiusPx, style.pointRadiusPx);
        }
        break;
    }
    default:
        break;
    }
}

} // namespace

TileDecoder::TileDecoder(QObject *parent)
    : QObject(parent)
{
    // один поток на GUI
    m_pool.setMaxThreadCount(qBound(1, QThread::idealThreadCount() - 1, k_MaxThreads));
}

TileDecoder::~TileDecoder()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void TileDecoder::decodeRaster(const mapGL::TileMeta &tile)
{
    m_pool.start([this, tile] {
        QImage img = decodeImage(tile.rwTile.data, tile.rwTile.format);
        // назад в главный поток объекта
        QMetaObject::invokeMethod(this,
            [this, key = tile.key, img = std::move(img), q = tile.rwTile.quality] {
                emit decodedRaster(key, img, q);
            },
            Qt::QueuedConnection);
    });
}

void TileDecoder::decodeVector(const mapGL::TileMeta &tile)
{
    m_pool.start([this, tile] {
        QImage img = rasterizeVector(tile.key, tile.rwTile.data, tile.rwTile.filter.get());
        QMetaObject::invokeMethod(this,
            [this, key = tile.key, img = std::move(img), q = tile.rwTile.quality] {
                emit decodedVector(key, img, q);
            },
            Qt::QueuedConnection);
    });
}

QImage TileDecoder::decodeImage(const QByteArray &data, const QByteArray &format)
{
    if (data.isEmpty())
        return QImage();

    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);

    QImageReader reader(&buffer, format);
    QImage img;
    if (!reader.read(&img)) {
        if (format.isEmpty())
            return QImage();
        buffer.seek(0);
        QImageReader autodetect(&buffer);
        if (!autodetect.read(&img))
            return QImage();
    }

    /*
     * Format_ARGB32_Premultiplied - цветовые каналы умножены на прозрачность
     * Format_RGB32 - канал прозрачности всегда заполнен значением 0xff
     * в процессе рендеринга игнорируется расчет смешивания цветов
     */
    const QImage::Format target = img.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied
                                                        : QImage::Format_RGB32;
    if (img.format() != target)
        img = img.convertToFormat(target);
    return img;
}

QImage TileDecoder::rasterizeVector(uint64_t key, const QByteArray &data,
                                    const mapGL::VectorFilter *filter)
{
    const QByteArray pbf = mapGL::mvt::gunzip(data);
    if (pbf.isEmpty())
        return QImage();

    QVector<mapGL::mvt::Layer> layers;
    if (!mapGL::mvt::parse(pbf, &layers))
        return QImage();

    uint8_t z;
    uint32_t x, y;
    mapGL::math::parseKey(key, &z, &x, &y);

    const double tileMeters = 40075016.686 * std::cos(qDegreesToRadians(tileCenterLat(y, z)))
                              / double(1u << z);
    const double metersPerPx = tileMeters / k_vectorTileSize;

    QImage img(k_vectorTileSize, k_vectorTileSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    for (const VectorStyle &style : k_vectorStyles) {
        for (const mapGL::mvt::Layer &layer : std::as_const(layers)) {
            if (layer.name != QLatin1String(style.layer))
                continue;
            const double scale = double(k_vectorTileSize) / double(layer.extent);
            for (const mapGL::mvt::Feature &feature : layer.features) {
                if (filter && !featureAllowed(feature, *filter, layer.extent, z, x, y))
                    continue;
                drawFeature(p, style, feature, scale, metersPerPx);
            }
        }
    }

    p.end();
    return img;
}
