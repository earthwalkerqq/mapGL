#ifndef VECTORTILE_H
#define VECTORTILE_H

#include <QByteArray>
#include <QHash>
#include <QPolygonF>
#include <QString>
#include <QVariant>
#include <QVector>

namespace mapGL {
namespace mvt {

enum class GeomType {
    Unknown    = 0,
    Point      = 1,
    LineString = 2,
    Polygon    = 3
};

struct Feature {
    GeomType type = GeomType::Unknown;
    QVector<QPolygonF> parts;
    QHash<QString, QVariant> properties;
};

struct Layer {
    QString name;
    quint32 extent = 4096;
    QVector<Feature> features;
};

QByteArray gunzip(const QByteArray &data);
bool parse(const QByteArray &pbf, QVector<Layer> *layers);

} // namespace mvt
} // namespace mapGL

#endif // VECTORTILE_H
