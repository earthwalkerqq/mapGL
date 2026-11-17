#ifndef TERMINALPROCEDUREBUILDER_H
#define TERMINALPROCEDUREBUILDER_H

/*
 * Поле RT_TYPE определяет сегмент ветки
 * SID:
 *  1/4 - rwy - от впп до общей части. TRANS_IDENT - точка на впп
 *  2/5 - common - общая часть
 *  3/6 - en_route - переход на трассу. TRANS_IDENT - точка выхода
 * STAR:
 *  1/4 - en_route - переход от трассы. TRANS_IDENT - точка входа
 *  2/5 - common - общая часть
 *  3/6 - rwy - переход к ВП
 * APPROACH:
 *  A - переход к заходу. TRANS_IDENT - начальная точка (IAF)
 *  I/L/R/D/N/V/S/Q/J/X/B - сам заход (ILS, LOC, RNAV, VOR/DME, NDB ...)
 *
 * Сегменты сходятся в общей точке: последняя точка одного сегмента
 * совпадает с первой точкой следующего.
 * RT_TYPE 4, RW06C : 10 CF  EE306
 * RT_TYPE 6, EMGAS : 10 IF  EE306
 *                    20 TF  RATKE
 */

#include <QHash>
#include <QList>
#include <QMap>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QVector>
#include <optional>

#include "orm.h"
#include "standartsourcepath.h"

namespace mapGL {
namespace proc {

enum class Kind {
    Sid,
    Star,
    Approach
};

enum class LegShape {
    Track,
    Arc,
    Vector,
    Hold
};

struct GeoPoint {
    double lat = 0.;
    double lon = 0.;
};

struct Fix {
    QString ident;
    GeoPoint pos;
    QString typePoint;
    QString descCode;
    bool flyOver = false;
    bool faf = false;
    bool map = false;
    QString altDesc;
    int h1 = 0;
    int h2 = 0;
    int speedKmh = 0;
    bool final = false;
};

struct Leg {
    QString pathTerm;
    QString fix;
    LegShape shape = LegShape::Track;
    bool estimated = false;
    bool missed = false;
    QVector<GeoPoint> points;
};

struct Procedure {
    Kind kind = Kind::Sid;
    QString icao;
    QString runway;
    QString name;
    QString transition;
    QString approachType;
    QVector<Leg> legs;
    QVector<Fix> fixes;

    const Fix *entry() const { return fixes.isEmpty() ? nullptr : &fixes.first(); }
    const Fix *exit() const { return fixes.isEmpty() ? nullptr : &fixes.last(); }
};

struct Runway {
    QString ident;
    GeoPoint threshold;
    GeoPoint departureEnd;
    double trueCourse = 0.;
    double elevation = 0.;
};

struct RunwayProcedures {
    QString icao;
    Runway runway;
    QVector<Procedure> sids;
    QVector<Procedure> stars;
    QVector<Procedure> approaches;

    bool isEmpty() const { return sids.isEmpty() && stars.isEmpty() && approaches.isEmpty(); }
};

} // namespace proc
} // namespace mapGL

class TerminalProcedureBuilder
{
public:
    TerminalProcedureBuilder(const QString &icao, const QString &vpp);

    bool load(void);
    mapGL::proc::RunwayProcedures build(void) const;

    QStringList availableRunways(void) const { return m_thresholds.keys(); }
    QString lastError(void) const { return m_lastError; }

private:
    using Local = QPointF;

    struct RawLeg {
        QString route;
        QString rtType;
        QString trans;
        int seq = 0;
        QString pathTerm;
        QString fix;
        QString typePoint;
        QString descCode;
        QString desc41;
        QString desc43;
        std::optional<Local> fixPos;
        double magCourse = 0.;
        bool hasCourse = false;
        QString turnDir;
        double arcRad = 0.;
        std::optional<Local> center;
        std::optional<Local> navaid;
        double rho = 0.;
        double theta = 0.;
        QString altDesc;
        int h1 = 0;
        int h2 = 0;
        int speed = 0;
        QString distOrTime;
        double dist = 0.;
        bool endTrass = false;
    };

    struct Threshold {
        Local pos;
        double magCourse = 0.;
        double elevation = 0.;
    };

    using SegmentKey = QPair<QString, QString>;
    using Segments = QMap<SegmentKey, QVector<RawLeg>>;
    using Routes = QMap<QString, Segments>;

    template<typename Table>
    Routes readRoutes(void);

    static void appendIfValid(QVector<mapGL::proc::Procedure> &out, mapGL::proc::Procedure &&p);

    QVector<mapGL::proc::Procedure> assembleSid(const QString &name, const Segments &segs) const;
    QVector<mapGL::proc::Procedure> assembleStar(const QString &name, const Segments &segs) const;
    QVector<mapGL::proc::Procedure> assembleApproach(const QString &name, const Segments &segs) const;

    mapGL::proc::Procedure makeProcedure(mapGL::proc::Kind kind, const QString &name,
                                         const QString &runway, const QString &transition,
                                         const QVector<RawLeg> &legs,
                                         const std::optional<Local> &start, double startAlt,
                                         const std::optional<double> &startHeading = std::nullopt) const;

    void buildLeg(const QVector<RawLeg> &legs, int i, std::optional<Local> &cur,
                  std::optional<double> &heading, double &alt, bool missed,
                  mapGL::proc::Procedure &out) const;
    void roundFlyBy(mapGL::proc::Procedure &p) const;
    static void markFinal(mapGL::proc::Procedure &p, const QVector<RawLeg> &legs);

    std::optional<Local> departureEnd(const QString &runway) const;
    double departureElevation(const QString &runway) const;

    Local toLocal(double latRad, double lonRad) const;
    mapGL::proc::GeoPoint toGeo(const Local &p) const;
    double toTrue(double magRad) const;

    static QString runwayOf(const QString &trans);
    static QString runwayFromApproach(const QString &route);
    static bool matchesRunway(const QString &trans, const QString &runway);
    static QString approachTypeName(const QString &rtType);

    QString m_icao;
    QString m_vpp;
    DataBase m_db;
    QString m_lastError;

    double m_latRad = 0.;
    double m_lonRad = 0.;
    double m_magVarDeg = 0.;
    double m_elevation = 0.;

    QMap<QString, Threshold> m_thresholds;
    QHash<QString, double> m_runwayLength;

    Routes m_sid;
    Routes m_star;
    Routes m_approach;
};

#endif // TERMINALPROCEDUREBUILDER_H
