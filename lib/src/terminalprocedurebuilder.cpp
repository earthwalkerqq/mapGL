#include "terminalprocedurebuilder.h"

#include <QRegularExpression>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

using mapGL::proc::Kind;
using mapGL::proc::LegShape;

constexpr double k_earthRadiusM   = 6371008.8;
constexpr double k_climbGradient  = 0.033;
constexpr double k_minEstimatedM  = 1500.;
constexpr double k_fallbackLegM   = 5556.;
constexpr double k_vectorLegM     = 9260.;
constexpr double k_holdSpeedMs    = 120.;
constexpr double k_arcStepDeg     = 5.;
constexpr double k_maxReachM      = 150000.;
constexpr double k_defaultRunwayM = 3000.;
constexpr double k_turnSpeedMs    = 95.;
constexpr double k_bankDeg        = 25.;
constexpr double k_gravity        = 9.80665;
constexpr double k_minTurnDeg     = 2.;
constexpr double k_onCourseM      = 150.;
constexpr double k_interceptDeg   = 45.;
constexpr double k_maxFlyByDeg    = 150.;

const QSet<QString> k_sidRunway   = {QStringLiteral("1"), QStringLiteral("4")};
const QSet<QString> k_sidCommon   = {QStringLiteral("2"), QStringLiteral("5")};
const QSet<QString> k_sidEnroute  = {QStringLiteral("3"), QStringLiteral("6")};
const QSet<QString> k_starEnroute = {QStringLiteral("1"), QStringLiteral("4")};
const QSet<QString> k_starCommon  = {QStringLiteral("2"), QStringLiteral("5")};
const QSet<QString> k_starRunway  = {QStringLiteral("3"), QStringLiteral("6")};
const QString k_approachTransition = QStringLiteral("A");

QPointF dirOf(double trueDeg)
{
    const double b = qDegreesToRadians(trueDeg);
    return { std::sin(b), std::cos(b) };
}

double normalizeDeg(double deg)
{
    deg = std::fmod(deg, 360.);
    return deg < 0. ? deg + 360. : deg;
}

double bearing(const QPointF &from, const QPointF &to)
{
    return normalizeDeg(qRadiansToDegrees(std::atan2(to.x() - from.x(), to.y() - from.y())));
}

double distance(const QPointF &a, const QPointF &b)
{
    return std::hypot(b.x() - a.x(), b.y() - a.y());
}

QPointF forward(const QPointF &p, double trueDeg, double meters)
{
    return p + dirOf(trueDeg) * meters;
}

std::optional<QPointF> rayLine(const QPointF &p, double crs, const QPointF &q, double crs2,
                               bool secondIsRay)
{
    const QPointF d1 = dirOf(crs);
    const QPointF d2 = dirOf(crs2);
    const double den = d1.x() * d2.y() - d1.y() * d2.x();
    if (std::abs(den) < 1e-6)
        return std::nullopt;

    const QPointF w = q - p;
    const double t = (w.x() * d2.y() - w.y() * d2.x()) / den;
    const double s = (w.x() * d1.y() - w.y() * d1.x()) / den;
    if (t <= 0. || t > k_maxReachM || (secondIsRay && s < 0.))
        return std::nullopt;
    return p + d1 * t;
}

std::optional<QPointF> rayCircle(const QPointF &p, double crs, const QPointF &c, double r)
{
    const QPointF d = dirOf(crs);
    const QPointF f = p - c;
    const double b = f.x() * d.x() + f.y() * d.y();
    const double cc = f.x() * f.x() + f.y() * f.y() - r * r;
    const double disc = b * b - cc;
    if (disc < 0.)
        return std::nullopt;

    const double sq = std::sqrt(disc);
    for (const double t : {-b - sq, -b + sq}) {
        if (t > 1. && t < k_maxReachM)
            return p + d * t;
    }
    return std::nullopt;
}

QVector<QPointF> arcPoints(const QPointF &c, const QPointF &from, const QPointF &to, bool clockwise)
{
    const double a0 = bearing(c, from);
    const double a1 = bearing(c, to);
    const double r0 = distance(c, from);
    const double r1 = distance(c, to);

    double sweep = clockwise ? normalizeDeg(a1 - a0) : -normalizeDeg(a0 - a1);
    if (std::abs(sweep) < 1e-6)
        sweep = clockwise ? 360. : -360.;

    const int steps = qMax(2, int(std::ceil(std::abs(sweep) / k_arcStepDeg)));
    QVector<QPointF> out;
    out.reserve(steps);
    for (int k = 1; k <= steps; ++k) {
        const double f = double(k) / steps;
        out.push_back(forward(c, a0 + sweep * f, r0 + (r1 - r0) * f));
    }
    return out;
}

QVector<QPointF> holdingPoints(const QPointF &fix, double inboundTrue, bool rightTurns,
                               double legM, double radius)
{
    const QPointF d = dirOf(inboundTrue);
    const QPointF right(d.y(), -d.x());
    const QPointF side = rightTurns ? right : -right;

    const QPointF c1 = fix + side * radius;
    const QPointF outboundStart = fix + side * (2. * radius);
    const QPointF outboundEnd = outboundStart - d * legM;
    const QPointF c2 = outboundEnd - side * radius;
    const QPointF inboundStart = fix - d * legM;

    QVector<QPointF> out;
    out += arcPoints(c1, fix, outboundStart, rightTurns);
    out.push_back(outboundEnd);
    out += arcPoints(c2, outboundEnd, inboundStart, rightTurns);
    out.push_back(fix);
    return out;
}

double turnRadius(int speedKmh)
{
    const double v = speedKmh > 0 ? speedKmh / 3.6 : k_turnSpeedMs;
    return v * v / (k_gravity * std::tan(qDegreesToRadians(k_bankDeg)));
}

double turnAngle(double from, double to, const QString &dir)
{
    const double right = normalizeDeg(to - from);
    if (right < k_minTurnDeg || right > 360. - k_minTurnDeg)
        return 0.;
    if (dir == QLatin1String("R"))
        return right;
    if (dir == QLatin1String("L"))
        return right - 360.;
    return right <= 180. ? right : right - 360.;
}

QVector<QPointF> turnArc(const QPointF &p, double heading, double sweep, double radius)
{
    QVector<QPointF> out;
    if (std::abs(sweep) < k_minTurnDeg || radius <= 0.)
        return out;

    const double side = sweep > 0. ? 90. : -90.;
    const QPointF c = forward(p, heading + side, radius);
    const double a0 = heading - side;
    const int steps = qMax(2, int(std::ceil(std::abs(sweep) / k_arcStepDeg)));
    out.reserve(steps);
    for (int k = 1; k <= steps; ++k)
        out.push_back(forward(c, a0 + sweep * double(k) / steps, radius));
    return out;
}

QVector<QPointF> turnToFix(const QPointF &p, double heading, const QPointF &fix,
                           const QString &dir, double radius)
{
    const double rel = turnAngle(heading, bearing(p, fix), QString());
    if (rel == 0.)
        return {};

    bool right = rel > 0.;
    if (dir == QLatin1String("R"))
        right = true;
    else if (dir == QLatin1String("L"))
        right = false;

    const double side = right ? 90. : -90.;
    double r = radius;
    QPointF c = forward(p, heading + side, r);
    double d = distance(c, fix);
    for (int k = 0; k < 6 && d <= r * 1.05; ++k) {
        r *= 0.5;
        c = forward(p, heading + side, r);
        d = distance(c, fix);
    }
    if (d <= r)
        return {};

    const double off = qRadiansToDegrees(std::acos(r / d));
    const double bc = bearing(c, fix);
    const double a0 = heading - side;
    const double a1 = right ? bc - off : bc + off;
    const double sweep = right ? normalizeDeg(a1 - a0) : -normalizeDeg(a0 - a1);
    return turnArc(p, heading, sweep, r);
}

QString oppositeRunway(const QString &rw)
{
    bool ok = false;
    const int num = rw.left(2).toInt(&ok);
    if (!ok)
        return QString();

    const int opp = (num + 18 - 1) % 36 + 1;
    QString side = rw.mid(2);
    if (side == QLatin1String("L"))
        side = QStringLiteral("R");
    else if (side == QLatin1String("R"))
        side = QStringLiteral("L");
    return QStringLiteral("%1%2").arg(opp, 2, 10, QLatin1Char('0')).arg(side);
}

} // namespace

TerminalProcedureBuilder::TerminalProcedureBuilder(const QString &icao, const QString &vpp)
    : m_icao(icao.trimmed().toUpper())
    , m_vpp(vpp.trimmed().toUpper())
    , m_db(mapGL::aeroSourceFile())
{
    if (m_vpp.startsWith(QLatin1String("RW")))
        m_vpp = m_vpp.mid(2);
}

bool TerminalProcedureBuilder::load(void)
{
    if (!m_db.connect()) {
        m_lastError = m_db.lastError();
        return false;
    }

    const QPair<Aerodrom, bool> aero = m_db.from<Aerodrom>()
        .select(AerodromFields::lat_rad, AerodromFields::long_rad,
                AerodromFields::mag_var_rad, AerodromFields::lev_kta)
        .where(AerodromFields::kod_icao, Comparison::Equal, m_icao)
        .first();
    if (!aero.second) {
        m_lastError = QStringLiteral("аэродром %1 не найден").arg(m_icao);
        return false;
    }

    m_latRad = aero.first.lat_rad;
    m_lonRad = aero.first.long_rad;
    m_magVarDeg = qRadiansToDegrees(aero.first.mag_var_rad);
    m_elevation = aero.first.lev_kta;

    const QList<KursPos> thresholds = m_db.from<KursPos>()
        .select(KursPosFields::num_vpp, KursPosFields::lat_rad, KursPosFields::long_rad,
                KursPosFields::rw_mag, KursPosFields::thres_elev)
        .where(KursPosFields::kod_icao, Comparison::Equal, m_icao)
        .exec();
    for (const KursPos &t : thresholds) {
        m_thresholds.insert(t.num_vpp.trimmed(),
                            Threshold{toLocal(t.lat_rad, t.long_rad), t.rw_mag, t.thres_elev});
    }

    const QList<Ranway> runways = m_db.from<Ranway>()
        .select(RanwayFields::num_vpp, RanwayFields::rw_length)
        .where(RanwayFields::kod_icao, Comparison::Equal, m_icao)
        .exec();
    for (const Ranway &r : runways) {
        if (!r.num_vpp.trimmed().isEmpty() && r.rw_length > 0)
            m_runwayLength.insert(r.num_vpp.trimmed(), double(r.rw_length));
    }

    if (m_vpp.isEmpty() || !m_thresholds.contains(m_vpp)) {
        m_lastError = QStringLiteral("ВПП %1 не найдена у %2, доступны: %3")
                          .arg(m_vpp, m_icao, availableRunways().join(QStringLiteral(", ")));
        return false;
    }

    m_sid = readRoutes<PD_VIHOD>();
    m_star = readRoutes<PE_VHOD>();
    m_approach = readRoutes<PF_POSADKA>();
    return true;
}

template<typename Table>
TerminalProcedureBuilder::Routes TerminalProcedureBuilder::readRoutes(void)
{
    using F = typename Table::fields_type;

    const QList<Table> rows = m_db.from<Table>()
        .select()
        .where(F::kod_icao, Comparison::Equal, m_icao)
        .order(F::route_ident, F::rt_type, F::trans_ident, F::seq_nr)
        .exec();

    Routes routes;
    for (const Table &r : rows) {
        RawLeg l;
        l.route = r.route_ident.trimmed();
        l.rtType = r.rt_type.trimmed();
        l.trans = r.trans_ident.trimmed();
        l.seq = r.seq_nr;
        l.pathTerm = r.path_term.trimmed().toUpper();
        l.fix = r.fix_ident.trimmed();
        l.typePoint = r.type_point.trimmed();
        l.descCode = r.desc_code;
        l.desc41 = r.desc_41.trimmed();
        l.desc43 = r.desc_43.trimmed();
        if (!l.fix.isEmpty() && (r.lat_rad_pm != 0. || r.long_rad_pm != 0.))
            l.fixPos = toLocal(r.lat_rad_pm, r.long_rad_pm);
        l.magCourse = r.ob_mag_crs;
        l.hasCourse = r.ob_mag_crs != 0.;
        l.turnDir = r.turn_dir.trimmed().toUpper();
        l.arcRad = r.arc_rad;
        if (r.lat_rad_ckt != 0. || r.long_rad_ckt != 0.)
            l.center = toLocal(r.lat_rad_ckt, r.long_rad_ckt);
        if (r.lat_rad_nav != 0. || r.long_rad_nav != 0.)
            l.navaid = toLocal(r.lat_rad_nav, r.long_rad_nav);
        l.rho = r.rho;
        l.theta = r.theta;
        l.altDesc = r.alt_desc.trimmed();
        l.h1 = r.h1;
        l.h2 = r.h2;
        l.speed = r.speed_limit;
        l.distOrTime = r.dist_or_time.trimmed().toUpper();
        l.dist = r.rte_dist;
        l.endTrass = r.end_trass != 0;

        routes[l.route][{l.rtType, l.trans}].push_back(l);
    }
    return routes;
}

mapGL::proc::RunwayProcedures TerminalProcedureBuilder::build(void) const
{
    mapGL::proc::RunwayProcedures result;
    result.icao = m_icao;

    const auto thr = m_thresholds.constFind(m_vpp);
    if (thr == m_thresholds.cend())
        return result;

    result.runway.ident = m_vpp;
    result.runway.threshold = toGeo(thr->pos);
    result.runway.trueCourse = toTrue(thr->magCourse);
    result.runway.elevation = thr->elevation != 0. ? thr->elevation : m_elevation;
    if (const std::optional<Local> end = departureEnd(m_vpp))
        result.runway.departureEnd = toGeo(*end);

    for (auto it = m_sid.cbegin(); it != m_sid.cend(); ++it)
        result.sids += assembleSid(it.key(), it.value());
    for (auto it = m_star.cbegin(); it != m_star.cend(); ++it)
        result.stars += assembleStar(it.key(), it.value());
    for (auto it = m_approach.cbegin(); it != m_approach.cend(); ++it)
        result.approaches += assembleApproach(it.key(), it.value());

    return result;
}

void TerminalProcedureBuilder::appendIfValid(QVector<mapGL::proc::Procedure> &out,
                                             mapGL::proc::Procedure &&p)
{
    int points = 0;
    for (const mapGL::proc::Leg &leg : std::as_const(p.legs))
        points += int(leg.points.size());
    if (points >= 2)
        out.push_back(std::move(p));
}

QVector<mapGL::proc::Procedure> TerminalProcedureBuilder::assembleSid(const QString &name,
                                                                      const Segments &segs) const
{
    QVector<SegmentKey> runwaySegs, commonSegs, enrouteSegs;
    for (auto it = segs.cbegin(); it != segs.cend(); ++it) {
        if (k_sidRunway.contains(it.key().first))       runwaySegs.push_back(it.key());
        else if (k_sidCommon.contains(it.key().first))  commonSegs.push_back(it.key());
        else if (k_sidEnroute.contains(it.key().first)) enrouteSegs.push_back(it.key());
    }

    QVector<RawLeg> body;
    if (!runwaySegs.isEmpty()) {
        bool found = false;
        for (const SegmentKey &k : std::as_const(runwaySegs)) {
            if (matchesRunway(k.second, m_vpp)) {
                body = segs.value(k);
                found = true;
                break;
            }
        }
        if (!found)
            return {};
    }

    bool commonMatched = false;
    for (const SegmentKey &k : std::as_const(commonSegs)) {
        if (matchesRunway(k.second, m_vpp)) {
            body += segs.value(k);
            commonMatched = true;
        }
    }
    if (runwaySegs.isEmpty() && !commonMatched)
        return {};

    const std::optional<Local> origin = departureEnd(m_vpp);
    const double elevation = departureElevation(m_vpp);
    std::optional<double> heading;
    if (const auto thr = m_thresholds.constFind(m_vpp); thr != m_thresholds.cend())
        heading = toTrue(thr->magCourse);

    QVector<mapGL::proc::Procedure> out;
    if (enrouteSegs.isEmpty()) {
        appendIfValid(out, makeProcedure(Kind::Sid, name, m_vpp, QString(), body, origin, elevation,
                                         heading));
        return out;
    }
    for (const SegmentKey &e : std::as_const(enrouteSegs)) {
        appendIfValid(out, makeProcedure(Kind::Sid, name, m_vpp, e.second,
                                         body + segs.value(e), origin, elevation, heading));
    }
    return out;
}

QVector<mapGL::proc::Procedure> TerminalProcedureBuilder::assembleStar(const QString &name,
                                                                       const Segments &segs) const
{
    QVector<SegmentKey> enrouteSegs, commonSegs, runwaySegs;
    for (auto it = segs.cbegin(); it != segs.cend(); ++it) {
        if (k_starEnroute.contains(it.key().first))      enrouteSegs.push_back(it.key());
        else if (k_starCommon.contains(it.key().first))  commonSegs.push_back(it.key());
        else if (k_starRunway.contains(it.key().first))  runwaySegs.push_back(it.key());
    }

    QVector<RawLeg> tail;
    if (!runwaySegs.isEmpty()) {
        bool found = false;
        for (const SegmentKey &k : std::as_const(runwaySegs)) {
            if (matchesRunway(k.second, m_vpp)) {
                tail = segs.value(k);
                found = true;
                break;
            }
        }
        if (!found)
            return {};
    }

    QVector<RawLeg> common;
    bool commonMatched = false;
    for (const SegmentKey &k : std::as_const(commonSegs)) {
        if (matchesRunway(k.second, m_vpp)) {
            common += segs.value(k);
            commonMatched = true;
        }
    }
    if (runwaySegs.isEmpty() && !commonSegs.isEmpty() && !commonMatched)
        return {};

    QVector<mapGL::proc::Procedure> out;
    if (enrouteSegs.isEmpty()) {
        appendIfValid(out, makeProcedure(Kind::Star, name, m_vpp, QString(),
                                         common + tail, std::nullopt, 0.));
        return out;
    }
    for (const SegmentKey &e : std::as_const(enrouteSegs)) {
        appendIfValid(out, makeProcedure(Kind::Star, name, m_vpp, e.second,
                                         segs.value(e) + common + tail, std::nullopt, 0.));
    }
    return out;
}

QVector<mapGL::proc::Procedure> TerminalProcedureBuilder::assembleApproach(const QString &name,
                                                                           const Segments &segs) const
{
    if (runwayFromApproach(name) != m_vpp)
        return {};

    QVector<SegmentKey> transitions, finals;
    for (auto it = segs.cbegin(); it != segs.cend(); ++it) {
        if (it.key().first == k_approachTransition)
            transitions.push_back(it.key());
        else
            finals.push_back(it.key());
    }

    QVector<mapGL::proc::Procedure> out;
    for (const SegmentKey &f : std::as_const(finals)) {
        const QString type = approachTypeName(f.first);

        if (transitions.isEmpty()) {
            mapGL::proc::Procedure p = makeProcedure(Kind::Approach, name, m_vpp, QString(),
                                                     segs.value(f), std::nullopt, 0.);
            p.approachType = type;
            appendIfValid(out, std::move(p));
            continue;
        }
        for (const SegmentKey &t : std::as_const(transitions)) {
            mapGL::proc::Procedure p = makeProcedure(Kind::Approach, name, m_vpp, t.second,
                                                     segs.value(t) + segs.value(f),
                                                     std::nullopt, 0.);
            p.approachType = type;
            appendIfValid(out, std::move(p));
        }
    }
    return out;
}

mapGL::proc::Procedure TerminalProcedureBuilder::makeProcedure(Kind kind, const QString &name,
                                                               const QString &runway,
                                                               const QString &transition,
                                                               const QVector<RawLeg> &legs,
                                                               const std::optional<Local> &start,
                                                               double startAlt,
                                                               const std::optional<double> &startHeading) const
{
    mapGL::proc::Procedure p;
    p.kind = kind;
    p.icao = m_icao;
    p.runway = runway;
    p.name = name;
    p.transition = transition;

    int mapIndex = -1;
    if (kind == Kind::Approach) {
        for (int i = 0; i < legs.size(); ++i) {
            if (legs[i].desc43 == QLatin1String("M")) {
                mapIndex = i;
                break;
            }
        }
    }

    std::optional<Local> cur = start;
    std::optional<double> heading = start ? startHeading : std::nullopt;
    double alt = startAlt;
    for (int i = 0; i < legs.size(); ++i)
        buildLeg(legs, i, cur, heading, alt, mapIndex >= 0 && i > mapIndex, p);

    roundFlyBy(p);
    markFinal(p, legs);
    return p;
}

void TerminalProcedureBuilder::buildLeg(const QVector<RawLeg> &legs, int i,
                                        std::optional<Local> &cur, std::optional<double> &heading,
                                        double &alt, bool missed,
                                        mapGL::proc::Procedure &out) const
{
    const RawLeg &l = legs[i];
    const QString &pt = l.pathTerm;
    const double crs = toTrue(l.magCourse);
    const double radius = turnRadius(l.speed);

    mapGL::proc::Leg leg;
    leg.pathTerm = pt;
    leg.fix = l.fix;
    leg.missed = missed;

    QVector<QPointF> pts;
    auto begin = [&]() {
        if (cur && pts.isEmpty())
            pts.push_back(*cur);
    };
    auto moveTo = [&](const QPointF &p) {
        begin();
        if (pts.isEmpty() || distance(pts.last(), p) > 1.)
            pts.push_back(p);
        cur = p;
    };
    auto appendArc = [&](const QVector<QPointF> &arc) {
        if (arc.isEmpty())
            return;
        begin();
        pts += arc;
        cur = arc.last();
    };
    auto turnOnto = [&](double course) {
        if (cur && heading)
            appendArc(turnArc(*cur, *heading, turnAngle(*heading, course, l.turnDir), radius));
        heading = course;
    };
    auto directTo = [&](const QPointF &fix) {
        if (cur && heading)
            appendArc(turnToFix(*cur, *heading, fix, l.turnDir, radius));
        moveTo(fix);
    };
    auto startAtFix = [&]() -> bool {
        if (l.fixPos) {
            if (cur && distance(*cur, *l.fixPos) > 1.) {
                const double h = bearing(*cur, *l.fixPos);
                moveTo(*l.fixPos);
                heading = h;
            } else {
                cur = l.fixPos;
            }
        }
        return cur.has_value();
    };
    auto estimate = [&](double meters) {
        moveTo(forward(*cur, crs, meters));
        leg.estimated = true;
    };

    if (pt == QLatin1String("IF")) {
        if (l.fixPos) {
            pts = {*l.fixPos};
            cur = l.fixPos;
        }
    } else if (pt == QLatin1String("DF") && l.fixPos) {
        directTo(*l.fixPos);
    } else if (pt == QLatin1String("CF") && l.fixPos) {
        if (cur && heading && l.hasCourse) {
            const QPointF d = dirOf(crs);
            const QPointF v = *cur - *l.fixPos;
            const double cross = d.x() * v.y() - d.y() * v.x();
            const double along = -(d.x() * v.x() + d.y() * v.y());
            if (along <= 0.) {
                directTo(*l.fixPos);
            } else if (std::abs(cross) < k_onCourseM) {
                turnOnto(crs);
                moveTo(*l.fixPos);
            } else {
                const double cut = crs + (cross < 0. ? -k_interceptDeg : k_interceptDeg);
                turnOnto(cut);
                const std::optional<QPointF> join = rayLine(*cur, cut, *l.fixPos, crs, false);
                if (join && QPointF::dotProduct(d, *l.fixPos - *join) > 0.) {
                    moveTo(*join);
                    heading = cut;
                    turnOnto(crs);
                    moveTo(*l.fixPos);
                } else {
                    directTo(*l.fixPos);
                }
            }
        } else {
            moveTo(*l.fixPos);
        }
    } else if (pt == QLatin1String("RF") && l.fixPos && cur) {
        std::optional<QPointF> center = l.center;
        if (!center && l.arcRad > 0. && heading) {
            const double side = l.turnDir == QLatin1String("L") ? -90. : 90.;
            center = forward(*cur, *heading + side, l.arcRad);
        }
        if (center) {
            begin();
            pts += arcPoints(*center, *cur, *l.fixPos, l.turnDir != QLatin1String("L"));
            leg.shape = LegShape::Arc;
            cur = l.fixPos;
        } else {
            moveTo(*l.fixPos);
        }
    } else if (pt == QLatin1String("AF") && l.fixPos && l.navaid && cur) {
        const double arc = l.rho > 0. ? l.rho : distance(*l.navaid, *l.fixPos);
        const QPointF onArc = forward(*l.navaid, bearing(*l.navaid, *cur), arc);
        moveTo(onArc);
        pts += arcPoints(*l.navaid, onArc, *l.fixPos, l.turnDir == QLatin1String("R"));
        leg.shape = LegShape::Arc;
        cur = l.fixPos;
    } else if (pt == QLatin1String("CA") || pt == QLatin1String("VA") || pt == QLatin1String("FA")) {
        const bool ok = (pt == QLatin1String("FA")) ? startAtFix() : cur.has_value();
        if (ok && l.hasCourse) {
            turnOnto(crs);
            const double climb = l.h1 > alt ? (l.h1 - alt) / k_climbGradient : 0.;
            estimate(qMax(k_minEstimatedM, climb));
        }
    } else if (pt == QLatin1String("CD") || pt == QLatin1String("VD") || pt == QLatin1String("FD")) {
        const bool ok = (pt == QLatin1String("FD")) ? startAtFix() : cur.has_value();
        if (ok && l.hasCourse) {
            turnOnto(crs);
            std::optional<QPointF> end;
            if (l.navaid && l.dist > 0.)
                end = rayCircle(*cur, crs, *l.navaid, l.dist);
            if (end)
                moveTo(*end);
            else
                estimate(l.dist > 0. ? l.dist : k_fallbackLegM);
        }
    } else if (pt == QLatin1String("CR") || pt == QLatin1String("VR")) {
        if (cur && l.hasCourse) {
            turnOnto(crs);
            std::optional<QPointF> end;
            if (l.navaid)
                end = rayLine(*cur, crs, *l.navaid, toTrue(l.theta), true);
            if (end)
                moveTo(*end);
            else
                estimate(k_fallbackLegM);
        }
    } else if (pt == QLatin1String("CI") || pt == QLatin1String("VI")) {
        if (cur && l.hasCourse) {
            turnOnto(crs);
            std::optional<QPointF> end;
            if (i + 1 < legs.size()) {
                const RawLeg &next = legs[i + 1];
                if (next.pathTerm == QLatin1String("AF") && next.navaid && next.rho > 0.)
                    end = rayCircle(*cur, crs, *next.navaid, next.rho);
                else if (next.fixPos && next.hasCourse)
                    end = rayLine(*cur, crs, *next.fixPos, toTrue(next.magCourse), false);
            }
            if (end)
                moveTo(*end);
            else
                estimate(k_fallbackLegM);
        }
    } else if (pt == QLatin1String("VM") || pt == QLatin1String("FM")) {
        const bool ok = (pt == QLatin1String("FM")) ? startAtFix() : cur.has_value();
        if (ok && l.hasCourse) {
            turnOnto(crs);
            estimate(k_vectorLegM);
            leg.shape = LegShape::Vector;
        }
    } else if (pt == QLatin1String("FC")) {
        if (startAtFix() && l.hasCourse) {
            turnOnto(crs);
            moveTo(forward(*cur, crs, l.dist > 0. ? l.dist : k_fallbackLegM));
            leg.estimated = l.dist <= 0.;
        }
    } else if (pt == QLatin1String("HA") || pt == QLatin1String("HF") || pt == QLatin1String("HM")) {
        const std::optional<QPointF> fix = l.fixPos ? l.fixPos : cur;
        if (fix) {
            double legM = 60. * k_holdSpeedMs;
            if (l.distOrTime == QLatin1String("D") && l.dist > 0.)
                legM = l.dist;
            else if (l.distOrTime == QLatin1String("T") && l.dist > 0.)
                legM = l.dist * 60. * k_holdSpeedMs;

            const double inbound = l.hasCourse ? crs : (cur ? bearing(*cur, *fix) : 0.);
            const double holdRadius = k_holdSpeedMs * 60. / M_PI;

            moveTo(*fix);
            pts += holdingPoints(*fix, inbound, l.turnDir != QLatin1String("L"), legM, holdRadius);
            leg.shape = LegShape::Hold;
            cur = fix;
            heading = inbound;
        }
    } else if (pt == QLatin1String("PI")) {
        if (startAtFix() && l.hasCourse) {
            const double turn = l.turnDir == QLatin1String("L") ? -45. : 45.;
            turnOnto(crs);
            moveTo(forward(*cur, crs, k_fallbackLegM));
            moveTo(forward(*cur, crs + turn, k_fallbackLegM / 2.));
            leg.estimated = true;
        }
    } else if (l.fixPos) {
        moveTo(*l.fixPos);
    }

    if (leg.shape != LegShape::Hold && pts.size() >= 2) {
        const QPointF &a = pts[pts.size() - 2];
        const QPointF &b = pts.last();
        if (distance(a, b) > 1.)
            heading = bearing(a, b);
    }

    if (l.h1 > 0)
        alt = (l.altDesc == QLatin1String("B") && l.h2 > 0) ? l.h2 : l.h1;

    if (l.fixPos) {
        mapGL::proc::Fix f;
        f.ident = l.fix;
        f.pos = toGeo(*l.fixPos);
        f.typePoint = l.typePoint;
        f.descCode = l.descCode;
        f.flyOver = l.desc41 == QLatin1String("Y");
        f.faf = l.desc43 == QLatin1String("F");
        f.map = l.desc43 == QLatin1String("M");
        f.altDesc = l.altDesc;
        f.h1 = l.h1;
        f.h2 = l.h2;
        f.speedKmh = l.speed;

        if (!out.fixes.isEmpty() && out.fixes.last().ident == f.ident) {
            mapGL::proc::Fix &last = out.fixes.last();
            if (f.h1 > 0) {
                last.altDesc = f.altDesc;
                last.h1 = f.h1;
                last.h2 = f.h2;
            }
            if (f.speedKmh > 0)
                last.speedKmh = f.speedKmh;
            last.flyOver |= f.flyOver;
            last.faf |= f.faf;
            last.map |= f.map;
        } else {
            out.fixes.push_back(f);
        }
    }

    if (!pts.isEmpty()) {
        leg.points.reserve(pts.size());
        for (const QPointF &p : std::as_const(pts))
            leg.points.push_back(toGeo(p));
        out.legs.push_back(std::move(leg));
    }
}

void TerminalProcedureBuilder::roundFlyBy(mapGL::proc::Procedure &p) const
{
    auto local = [this](const mapGL::proc::GeoPoint &g) {
        return toLocal(qDegreesToRadians(g.lat), qDegreesToRadians(g.lon));
    };

    for (int k = 0; k + 1 < p.legs.size(); ++k) {
        mapGL::proc::Leg &a = p.legs[k];
        mapGL::proc::Leg &b = p.legs[k + 1];
        if (a.shape != LegShape::Track || b.shape != LegShape::Track || a.missed != b.missed)
            continue;
        if (a.points.size() < 2 || b.points.size() < 2 || a.fix.isEmpty())
            continue;

        const mapGL::proc::Fix *fix = nullptr;
        for (const mapGL::proc::Fix &f : std::as_const(p.fixes)) {
            if (f.ident == a.fix) {
                fix = &f;
                break;
            }
        }
        if (!fix || fix->flyOver)
            continue;

        const QPointF prev = local(a.points[a.points.size() - 2]);
        const QPointF corner = local(a.points.last());
        const QPointF next = local(b.points[1]);
        if (distance(corner, local(b.points.first())) > 1.)
            continue;

        const double inCrs = bearing(prev, corner);
        const double delta = turnAngle(inCrs, bearing(corner, next), QString());
        if (std::abs(delta) < 3. || std::abs(delta) > k_maxFlyByDeg)
            continue;

        const double half = std::tan(qDegreesToRadians(std::abs(delta) / 2.));
        double r = turnRadius(fix->speedKmh);
        double d = r * half;
        const double limit = 0.5 * qMin(distance(prev, corner), distance(corner, next));
        if (d > limit) {
            d = limit;
            r = d / half;
        }

        const QPointF t1 = forward(corner, inCrs, -d);
        const QVector<QPointF> arc = turnArc(t1, inCrs, delta, r);
        if (arc.isEmpty())
            continue;

        a.points.last() = toGeo(t1);
        for (const QPointF &q : arc)
            a.points.push_back(toGeo(q));
        b.points.first() = toGeo(arc.last());
    }
}

void TerminalProcedureBuilder::markFinal(mapGL::proc::Procedure &p, const QVector<RawLeg> &legs)
{
    if (p.fixes.isEmpty())
        return;

    int index = -1;
    if (p.kind == Kind::Approach) {
        const QString rw = QStringLiteral("RW") + p.runway;
        for (int i = 0; i < p.fixes.size() && index < 0; ++i) {
            if (p.fixes[i].ident == rw)
                index = i;
        }
        for (int i = 0; i < p.fixes.size() && index < 0; ++i) {
            if (p.fixes[i].typePoint == QLatin1String("VPP"))
                index = i;
        }
        for (int i = 0; i < p.fixes.size() && index < 0; ++i) {
            if (p.fixes[i].map)
                index = i;
        }
    } else {
        for (int i = int(legs.size()) - 1; i >= 0 && index < 0; --i) {
            if (!legs[i].endTrass || !legs[i].fixPos)
                continue;
            for (int k = int(p.fixes.size()) - 1; k >= 0; --k) {
                if (p.fixes[k].ident == legs[i].fix) {
                    index = k;
                    break;
                }
            }
        }
    }

    if (index < 0)
        index = int(p.fixes.size()) - 1;
    p.fixes[index].final = true;
}

std::optional<QPointF> TerminalProcedureBuilder::departureEnd(const QString &runway) const
{
    if (runway.isEmpty())
        return std::nullopt;

    const auto opposite = m_thresholds.constFind(oppositeRunway(runway));
    if (opposite != m_thresholds.cend())
        return opposite->pos;

    const auto own = m_thresholds.constFind(runway);
    if (own != m_thresholds.cend()) {
        const double length = m_runwayLength.value(runway, k_defaultRunwayM);
        return forward(own->pos, toTrue(own->magCourse), length);
    }
    return std::nullopt;
}

double TerminalProcedureBuilder::departureElevation(const QString &runway) const
{
    const auto it = m_thresholds.constFind(runway);
    return (it != m_thresholds.cend() && it->elevation != 0.) ? it->elevation : m_elevation;
}

QPointF TerminalProcedureBuilder::toLocal(double latRad, double lonRad) const
{
    return { (lonRad - m_lonRad) * k_earthRadiusM * std::cos(m_latRad),
             (latRad - m_latRad) * k_earthRadiusM };
}

mapGL::proc::GeoPoint TerminalProcedureBuilder::toGeo(const QPointF &p) const
{
    return { qRadiansToDegrees(m_latRad + p.y() / k_earthRadiusM),
             qRadiansToDegrees(m_lonRad + p.x() / (k_earthRadiusM * std::cos(m_latRad))) };
}

double TerminalProcedureBuilder::toTrue(double magRad) const
{
    return normalizeDeg(qRadiansToDegrees(magRad) + m_magVarDeg);
}

QString TerminalProcedureBuilder::runwayOf(const QString &trans)
{
    return trans.startsWith(QLatin1String("RW")) ? trans.mid(2) : QString();
}

QString TerminalProcedureBuilder::runwayFromApproach(const QString &route)
{
    static const QRegularExpression re(QStringLiteral("(\\d{2}[LRC]?)"));
    const QRegularExpressionMatch m = re.match(route);
    return m.hasMatch() ? m.captured(1) : QString();
}

bool TerminalProcedureBuilder::matchesRunway(const QString &trans, const QString &runway)
{
    if (trans.isEmpty() || trans == QLatin1String("ALL") || runway.isEmpty())
        return true;

    const QString rw = runwayOf(trans);
    if (rw.isEmpty())
        return false;
    if (rw == runway)
        return true;
    return rw.endsWith(QLatin1Char('B')) && runway.startsWith(rw.chopped(1));
}

QString TerminalProcedureBuilder::approachTypeName(const QString &rtType)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("I"), QStringLiteral("ILS")},
        {QStringLiteral("L"), QStringLiteral("LOC")},
        {QStringLiteral("B"), QStringLiteral("LOC BC")},
        {QStringLiteral("R"), QStringLiteral("RNAV")},
        {QStringLiteral("D"), QStringLiteral("VOR/DME")},
        {QStringLiteral("S"), QStringLiteral("VOR")},
        {QStringLiteral("V"), QStringLiteral("VOR")},
        {QStringLiteral("N"), QStringLiteral("NDB")},
        {QStringLiteral("Q"), QStringLiteral("NDB/DME")},
        {QStringLiteral("J"), QStringLiteral("GLS")},
        {QStringLiteral("X"), QStringLiteral("LDA")},
    };
    return names.value(rtType, rtType);
}
