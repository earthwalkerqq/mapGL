#include "terminalprocedurelayer.h"

#include <QFont>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QSGTextNode>
#include <QSGTransformNode>
#include <QSet>
#include <QtMath>
#include <cmath>
#include <cstring>

namespace {

const QColor k_runwayColor(40, 40, 48);
const QColor k_lineColor(0, 32, 110);
const QColor k_missedColor(205, 85, 0);
const QColor k_fixColor(0, 0, 0);
const QColor k_holeColor(255, 255, 255);

QSGGeometryNode *makeNode(QSGNode *parent, const QColor &color)
{
    auto *node = new QSGGeometryNode;

    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);

    auto *material = new QSGFlatColorMaterial;
    material->setColor(color);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);

    parent->appendChildNode(node);
    return node;
}

void upload(QSGGeometryNode *node, const QVector<QSGGeometry::Point2D> &v)
{
    QSGGeometry *g = node->geometry();
    g->allocate(int(v.size()));
    if (!v.isEmpty()) {
        std::memcpy(g->vertexDataAsPoint2D(), v.constData(),
                    size_t(v.size()) * sizeof(QSGGeometry::Point2D));
    }
    node->markDirty(QSGNode::DirtyGeometry);
}

void push(QVector<QSGGeometry::Point2D> *v, const QPointF &p)
{
    v->push_back({float(p.x()), float(p.y())});
}

void addThickLine(QVector<QSGGeometry::Point2D> *v, const QPointF &a, const QPointF &b, double width)
{
    const QPointF d = b - a;
    const double len = std::hypot(d.x(), d.y());
    if (len < 0.5)
        return;
    const QPointF n(-d.y() / len * width / 2, d.x() / len * width / 2);
    for (const QPointF &p : {a + n, a - n, b + n, b + n, a - n, b - n})
        push(v, p);
}

void addJoin(QVector<QSGGeometry::Point2D> *v, const QPointF &c, double width)
{
    constexpr int k_segments = 8;
    const double r = width / 2;
    for (int i = 0; i < k_segments; ++i) {
        const double a0 = 2 * M_PI * i / k_segments;
        const double a1 = 2 * M_PI * (i + 1) / k_segments;
        push(v, c);
        push(v, c + QPointF(r * std::cos(a0), r * std::sin(a0)));
        push(v, c + QPointF(r * std::cos(a1), r * std::sin(a1)));
    }
}

void addArrowHead(QVector<QSGGeometry::Point2D> *v, const QPointF &c, const QPointF &dir, double size)
{
    const QPointF n(-dir.y(), dir.x());
    push(v, c + dir * size);
    push(v, c - dir * size * 0.6 + n * size * 0.75);
    push(v, c - dir * size * 0.6 - n * size * 0.75);
}

void addArrows(QVector<QSGGeometry::Point2D> *v, const QPointF &a, const QPointF &b,
               double size, double spacing)
{
    const QPointF d = b - a;
    const double len = std::hypot(d.x(), d.y());
    if (len < spacing * 0.75)
        return;
    const QPointF dir = d / len;
    const int count = qMax(1, int(len / spacing));
    for (int k = 1; k <= count; ++k)
        addArrowHead(v, a + d * (double(k) / (count + 1)), dir, size);
}

void addUpTriangle(QVector<QSGGeometry::Point2D> *v, const QPointF &c, double size)
{
    const double h = size * std::sqrt(3.) / 2.;
    push(v, c + QPointF(0., -h * 2. / 3.));
    push(v, c + QPointF(-size / 2., h / 3.));
    push(v, c + QPointF(size / 2., h / 3.));
}

} // namespace

void TerminalProcedureLayer::setSchemes(const mapGL::proc::RunwayProcedures &schemes)
{
    m_icao = schemes.icao;
    m_runway = schemes.runway.ident;
    m_runwayStart = toWorld(schemes.runway.threshold);
    m_runwayEnd = toWorld(schemes.runway.departureEnd);

    m_procedures.clear();
    auto append = [this](const QVector<mapGL::proc::Procedure> &list) {
        for (const mapGL::proc::Procedure &p : list) {
            DrawProcedure d;
            d.kind = p.kind;
            d.name = p.name;
            d.transition = p.transition;

            for (const mapGL::proc::Leg &leg : p.legs) {
                DrawLeg l;
                l.world.reserve(leg.points.size());
                for (const mapGL::proc::GeoPoint &g : leg.points)
                    l.world.push_back(toWorld(g));
                l.missed = leg.missed;
                l.arrows = leg.shape != mapGL::proc::LegShape::Hold;
                d.legs.push_back(std::move(l));
            }
            for (const mapGL::proc::Fix &f : p.fixes)
                d.fixes.push_back({toWorld(f.pos), f.ident, f.final});

            m_procedures.push_back(std::move(d));
        }
    };
    append(schemes.sids);
    append(schemes.stars);
    append(schemes.approaches);
    rebuildLabels();
}

void TerminalProcedureLayer::clear(void)
{
    m_icao.clear();
    m_runway.clear();
    m_procedures.clear();
    rebuildLabels();
}

QStringList TerminalProcedureLayer::names(Kind kind) const
{
    QStringList out;
    for (const DrawProcedure &p : m_procedures) {
        if (p.kind == kind && !out.contains(p.name))
            out.push_back(p.name);
    }
    return out;
}

QStringList TerminalProcedureLayer::transitions(Kind kind, const QString &route) const
{
    QStringList out;
    for (const DrawProcedure &p : m_procedures) {
        if (p.kind == kind && p.name == route && !p.transition.isEmpty() && !out.contains(p.transition))
            out.push_back(p.transition);
    }
    return out;
}

bool TerminalProcedureLayer::show(Kind kind, const QString &route, const QString &transition)
{
    bool found = false;
    for (DrawProcedure &p : m_procedures) {
        if (p.kind != kind || p.name != route)
            continue;
        if (!transition.isEmpty() && p.transition != transition)
            continue;
        p.visible = true;
        found = true;
    }
    rebuildLabels();
    return found;
}

void TerminalProcedureLayer::hide(Kind kind, const QString &route)
{
    for (DrawProcedure &p : m_procedures) {
        if (p.kind == kind && p.name == route)
            p.visible = false;
    }
    rebuildLabels();
}

void TerminalProcedureLayer::hideAll(void)
{
    for (DrawProcedure &p : m_procedures)
        p.visible = false;
    rebuildLabels();
}

bool TerminalProcedureLayer::hasVisible(void) const
{
    for (const DrawProcedure &p : m_procedures) {
        if (p.visible)
            return true;
    }
    return false;
}

QPointF TerminalProcedureLayer::toWorld(const mapGL::proc::GeoPoint &g)
{
    const double phi = qDegreesToRadians(g.lat);
    return { (g.lon + 180.0) / 360.0,
             (1.0 - std::log(std::tan(M_PI / 4 + phi / 2)) / M_PI) / 2.0 };
}

void TerminalProcedureLayer::appendProcedure(const DrawProcedure &p, const QTransform &w2s,
                                             double centerX, Vertices *lines, Vertices *missed,
                                             Vertices *outline, Vertices *hole,
                                             Vertices *fill) const
{
    if (p.legs.isEmpty() || p.legs.first().world.isEmpty())
        return;

    const double shift = std::round(centerX - p.legs.first().world.first().x());
    double prevX = p.legs.first().world.first().x() + shift;

    auto project = [&](const QPointF &w) {
        double x = w.x() + shift;
        x += std::round(prevX - x);
        prevX = x;
        return w2s.map(QPointF(x, w.y()));
    };

    for (const DrawLeg &leg : p.legs) {
        QVector<QPointF> s;
        s.reserve(leg.world.size());
        for (const QPointF &w : leg.world)
            s.push_back(project(w));

        Vertices *target = leg.missed ? missed : lines;
        for (int i = 0; i + 1 < s.size(); ++i) {
            addThickLine(target, s[i], s[i + 1], k_lineWidth);
            addJoin(target, s[i + 1], k_lineWidth);
            if (leg.arrows)
                addArrows(target, s[i], s[i + 1], k_arrowSize, k_arrowSpacing);
        }
    }

    for (const DrawFix &f : p.fixes) {
        const QPointF c = project(f.world);
        if (f.final) {
            addUpTriangle(fill, c, k_fixSize * 2.);
            continue;
        }
        addUpTriangle(outline, c, k_fixSize * 2.);
        addUpTriangle(hole, c, k_fixSize * 2. - k_fixBorder * 2. * std::sqrt(3.));
    }
}

void TerminalProcedureLayer::rebuildLabels(void)
{
    m_labels.clear();
    m_labelsDirty = true;

    QFont font;
    font.setPixelSize(k_labelPixelSize);
    font.setBold(true);

    QSet<QString> seen;
    for (const DrawProcedure &p : std::as_const(m_procedures)) {
        if (!p.visible)
            continue;
        for (const DrawFix &f : p.fixes) {
            const QString key = QStringLiteral("%1|%2|%3").arg(f.ident).arg(f.world.x(), 0, 'g', 12)
                                    .arg(f.world.y(), 0, 'g', 12);
            if (f.ident.isEmpty() || seen.contains(key))
                continue;
            seen.insert(key);

            auto layout = std::make_shared<QTextLayout>(f.ident, font);
            layout->beginLayout();
            QTextLine line = layout->createLine();
            line.setLineWidth(1000.);
            layout->endLayout();
            m_labels.push_back({f.world, std::move(layout)});
        }
    }
}

void TerminalProcedureLayer::updateLabels(QQuickWindow *window, const QTransform &w2s,
                                          double centerX)
{
    if (m_labelsDirty) {
        while (QSGNode *child = m_nodes.labels->firstChild()) {
            m_nodes.labels->removeChildNode(child);
            delete child;
        }
        if (window) {
            for (const Label &label : std::as_const(m_labels)) {
                auto *transform = new QSGTransformNode;
                QSGTextNode *text = window->createTextNode();
                text->setColor(k_fixColor);
                text->setTextStyle(QSGTextNode::Outline);
                text->setStyleColor(k_holeColor);
                text->addTextLayout(QPointF(0., 0.), label.layout.get());
                transform->appendChildNode(text);
                m_nodes.labels->appendChildNode(transform);
            }
        }
        m_labelsDirty = false;
    }

    QSGNode *child = m_nodes.labels->firstChild();
    for (const Label &label : std::as_const(m_labels)) {
        if (!child)
            break;
        const double shift = std::round(centerX - label.world.x());
        const QPointF s = w2s.map(label.world + QPointF(shift, 0.));
        const double height = label.layout->boundingRect().height();

        QMatrix4x4 m;
        m.translate(float(s.x() + k_fixSize + k_labelGap), float(s.y() - height / 2.));
        static_cast<QSGTransformNode *>(child)->setMatrix(m);
        child = child->nextSibling();
    }
}

void TerminalProcedureLayer::updateNodes(QSGNode *parent, QQuickWindow *window,
                                         const QTransform &worldToScreen, double centerWorldX)
{
    if (!parent)
        return;

    if (parent->childCount() == 0) {
        m_nodes.runway = makeNode(parent, k_runwayColor);
        m_nodes.lines = makeNode(parent, k_lineColor);
        m_nodes.missed = makeNode(parent, k_missedColor);
        m_nodes.fixOutline = makeNode(parent, k_fixColor);
        m_nodes.fixHole = makeNode(parent, k_holeColor);
        m_nodes.fixFill = makeNode(parent, k_fixColor);
        m_nodes.labels = new QSGNode;
        parent->appendChildNode(m_nodes.labels);
        m_labelsDirty = true;
    }

    Vertices runway, lines, missed, outline, hole, fill;

    if (hasVisible()) {
        const double shift = std::round(centerWorldX - m_runwayStart.x());
        addThickLine(&runway,
                     worldToScreen.map(m_runwayStart + QPointF(shift, 0.)),
                     worldToScreen.map(m_runwayEnd + QPointF(shift, 0.)),
                     k_runwayWidth);

        for (const DrawProcedure &p : std::as_const(m_procedures)) {
            if (p.visible)
                appendProcedure(p, worldToScreen, centerWorldX, &lines, &missed, &outline, &hole, &fill);
        }
    }

    upload(m_nodes.runway, runway);
    upload(m_nodes.lines, lines);
    upload(m_nodes.missed, missed);
    upload(m_nodes.fixOutline, outline);
    upload(m_nodes.fixHole, hole);
    upload(m_nodes.fixFill, fill);
    updateLabels(window, worldToScreen, centerWorldX);
}
