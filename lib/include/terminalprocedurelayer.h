#ifndef TERMINALPROCEDURELAYER_H
#define TERMINALPROCEDURELAYER_H

#include "terminalprocedurebuilder.h"

#include <QPointF>
#include <QSGGeometry>
#include <QStringList>
#include <QTextLayout>
#include <QTransform>
#include <QVector>
#include <memory>

class QQuickWindow;
class QSGGeometryNode;
class QSGNode;

class TerminalProcedureLayer
{
public:
    using Kind = mapGL::proc::Kind;

    void setSchemes(const mapGL::proc::RunwayProcedures &schemes);
    void clear(void);

    bool hasSchemes(void) const { return !m_procedures.isEmpty(); }
    QString icao(void) const { return m_icao; }
    QString runway(void) const { return m_runway; }

    QStringList names(Kind kind) const;
    QStringList transitions(Kind kind, const QString &route) const;

    bool show(Kind kind, const QString &route, const QString &transition = QString());
    void hide(Kind kind, const QString &route);
    void hideAll(void);
    bool hasVisible(void) const;

    void updateNodes(QSGNode *parent, QQuickWindow *window, const QTransform &worldToScreen,
                     double centerWorldX);

private:
    using Vertices = QVector<QSGGeometry::Point2D>;

    struct DrawLeg {
        QVector<QPointF> world;
        bool missed = false;
        bool arrows = true;
    };

    struct DrawFix {
        QPointF world;
        QString ident;
        bool final = false;
    };

    struct Label {
        QPointF world;
        std::shared_ptr<QTextLayout> layout;
    };

    struct DrawProcedure {
        Kind kind = Kind::Sid;
        QString name;
        QString transition;
        QVector<DrawLeg> legs;
        QVector<DrawFix> fixes;
        bool visible = false;
    };

    struct Nodes {
        QSGGeometryNode *runway = nullptr;
        QSGGeometryNode *lines = nullptr;
        QSGGeometryNode *missed = nullptr;
        QSGGeometryNode *fixOutline = nullptr;
        QSGGeometryNode *fixHole = nullptr;
        QSGGeometryNode *fixFill = nullptr;
        QSGNode *labels = nullptr;
    };

    void appendProcedure(const DrawProcedure &p, const QTransform &w2s, double centerX,
                         Vertices *lines, Vertices *missed,
                         Vertices *outline, Vertices *hole, Vertices *fill) const;
    void rebuildLabels(void);
    void updateLabels(QQuickWindow *window, const QTransform &w2s, double centerX);

    static QPointF toWorld(const mapGL::proc::GeoPoint &g);

    static constexpr double k_lineWidth = 2.5;
    static constexpr double k_runwayWidth = 6.0;
    static constexpr double k_arrowSize = 5.5;
    static constexpr double k_arrowSpacing = 160.0;
    static constexpr double k_fixSize = 7.0;
    static constexpr double k_fixBorder = 1.8;
    static constexpr int k_labelPixelSize = 12;
    static constexpr double k_labelGap = 4.0;

    QString m_icao;
    QString m_runway;
    QPointF m_runwayStart;
    QPointF m_runwayEnd;
    QVector<DrawProcedure> m_procedures;
    QVector<Label> m_labels;
    bool m_labelsDirty = true;

    Nodes m_nodes;
};

#endif // TERMINALPROCEDURELAYER_H
