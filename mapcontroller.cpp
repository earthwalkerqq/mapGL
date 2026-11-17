#include "mapcontroller.h"
#include "orm.h"
#include "standartsourcepath.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QtMath>
#include <algorithm>
#include <cmath>

MapController::MapController(QObject *parent)
    : QObject(parent) {}

void MapController::setMap(TileManager *map)
{
    if (m_map == map)
        return;
    if (m_map)
        disconnect(m_map, nullptr, this, nullptr);

    m_map = map;
    if (m_map) {
        connect(m_map, &TileManager::proceduresReady, this, &MapController::onProceduresReady);
        connect(m_map, &TileManager::proceduresFailed, this, &MapController::onProceduresFailed);
    }
    emit mapChanged();
}

void MapController::findAerodrome(const QString &icao)
{
    const QString code = icao.trimmed().toUpper();
    if (code.isEmpty()) {
        setStatus(QStringLiteral("Введите код ИКАО"));
        return;
    }

    const quint64 request = ++m_request;
    setBusy(true);
    setStatus(QStringLiteral("Поиск %1…").arg(code));

    auto *watcher = new QFutureWatcher<AerodromeInfo>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, request]() {
        watcher->deleteLater();
        if (request != m_request)
            return;
        onAerodromeRead(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run(&MapController::readAerodrome, code));
}

MapController::AerodromeInfo MapController::readAerodrome(const QString &icao)
{
    AerodromeInfo info;
    info.icao = icao;

    DataBase db(mapGL::aeroSourceFile());
    if (!db.connect()) {
        info.error = db.lastError();
        return info;
    }

    const QPair<Aerodrom, bool> aero = db.from<Aerodrom>()
        .select(AerodromFields::lat_rad, AerodromFields::long_rad)
        .where(AerodromFields::kod_icao, Comparison::Equal, icao)
        .first();
    if (!aero.second) {
        info.error = QStringLiteral("Аэродром %1 не найден").arg(icao);
        return info;
    }
    info.lat = qRadiansToDegrees(aero.first.lat_rad);
    info.lon = qRadiansToDegrees(aero.first.long_rad);

    const QList<KursPos> thresholds = db.from<KursPos>()
        .select(KursPosFields::num_vpp)
        .where(KursPosFields::kod_icao, Comparison::Equal, icao)
        .order(KursPosFields::num_vpp)
        .exec();
    for (const KursPos &t : thresholds) {
        const QString rw = t.num_vpp.trimmed();
        if (!rw.isEmpty() && !info.runways.contains(rw))
            info.runways.push_back(rw);
    }

    info.ok = true;
    return info;
}

void MapController::onAerodromeRead(const AerodromeInfo &info)
{
    setBusy(false);
    if (!info.ok) {
        setStatus(info.error);
        return;
    }

    resetProcedures();
    m_icao = info.icao;
    m_runways = info.runways;
    m_runway.clear();

    if (m_map && m_map->camera()) {
        m_map->camera()->setCenterLatLon(info.lat, info.lon);
        m_map->camera()->setZoom(k_aerodromeZoom);
    }

    emit aerodromeChanged();
    emit proceduresChanged();
    setStatus(m_runways.isEmpty()
                  ? QStringLiteral("%1: нет данных о ВПП").arg(m_icao)
                  : QStringLiteral("%1: выберите ВПП").arg(m_icao));
}

void MapController::selectRunway(const QString &runway)
{
    if (!m_map || m_icao.isEmpty() || !m_runways.contains(runway))
        return;

    resetProcedures();
    emit proceduresChanged();

    setBusy(true);
    setStatus(QStringLiteral("Строятся схемы %1 ВПП %2…").arg(m_icao, runway));
    m_map->loadProcedures(m_icao, runway);
}

void MapController::onProceduresReady(const QString &icao, const QString &runway)
{
    if (icao != m_icao)
        return;

    setBusy(false);
    m_runway = runway;
    for (int k = TileManager::Sid; k <= TileManager::Approach; ++k)
        m_names[k] = m_map ? m_map->procedureNames(k) : QStringList();

    emit proceduresChanged();
    setStatus(QStringLiteral("%1 ВПП %2: SID %3, STAR %4, заходов %5")
                  .arg(m_icao, m_runway)
                  .arg(m_names[TileManager::Sid].size())
                  .arg(m_names[TileManager::Star].size())
                  .arg(m_names[TileManager::Approach].size()));
}

void MapController::onProceduresFailed(const QString &icao, const QString &runway,
                                       const QString &error)
{
    Q_UNUSED(icao);
    Q_UNUSED(runway);
    setBusy(false);
    setStatus(error);
}

void MapController::showSid(const QString &route)
{
    showRoute(TileManager::Sid, route);
}

void MapController::showStar(const QString &route)
{
    showRoute(TileManager::Star, route);
}

void MapController::showApproach(const QString &route)
{
    showRoute(TileManager::Approach, route);
}

void MapController::showRoute(int kind, const QString &route)
{
    if (!m_map)
        return;

    if (!m_shown[kind].isEmpty()) {
        m_map->hideProcedure(kind, m_shown[kind]);
        m_shown[kind].clear();
    }
    if (route.isEmpty() || !m_names[kind].contains(route))
        return;

    if (m_map->showProcedure(kind, route))
        m_shown[kind] = route;
}

QVariantMap MapController::fixAt(qreal x, qreal y) const
{
    if (!m_map)
        return {};

    const std::optional<TerminalProcedureLayer::FixHit> hit = m_map->procedureFixAt(QPointF(x, y));
    if (!hit)
        return {};

    const mapGL::proc::Fix &f = hit->fix;
    QVariantList rows;
    auto row = [&rows](const QString &name, const QString &value) {
        if (!value.isEmpty())
            rows.push_back(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("value"), value}});
    };

    static const QStringList kinds = {QStringLiteral("SID"), QStringLiteral("STAR"),
                                      QStringLiteral("Заход")};
    QString procedure = kinds.value(int(hit->kind)) + QLatin1Char(' ') + hit->procedure;
    if (!hit->transition.isEmpty())
        procedure += QStringLiteral(" (%1)").arg(hit->transition);

    QStringList role;
    if (f.faf)
        role << QStringLiteral("FAF");
    if (f.map)
        role << QStringLiteral("MAPt");
    if (f.final)
        role << QStringLiteral("конечная");
    role << (f.flyOver ? QStringLiteral("с пролётом") : QStringLiteral("с упреждением"));

    row(QStringLiteral("Схема"), procedure);
    row(QStringLiteral("Тип"), f.typePoint);
    row(QStringLiteral("Высота"), altitudeText(f));
    row(QStringLiteral("Скорость"), f.speedKmh > 0 ? QStringLiteral("≤ %1 км/ч").arg(f.speedKmh) : QString());
    row(QStringLiteral("Признаки"), role.join(QStringLiteral(", ")));
    row(QStringLiteral("Широта"), coordinateText(f.pos.lat, true));
    row(QStringLiteral("Долгота"), coordinateText(f.pos.lon, false));

    return {
        {QStringLiteral("ident"), f.ident},
        {QStringLiteral("rows"), rows},
    };
}

QString MapController::altitudeText(const mapGL::proc::Fix &f)
{
    if (f.h1 <= 0)
        return QString();

    const QString h1 = QStringLiteral("%1 м").arg(f.h1);
    if (f.altDesc == QLatin1String("+"))
        return QStringLiteral("≥ ") + h1;
    if (f.altDesc == QLatin1String("-"))
        return QStringLiteral("≤ ") + h1;
    if (f.altDesc == QLatin1String("B") && f.h2 > 0)
        return QStringLiteral("%1 – %2 м").arg(qMin(f.h1, f.h2)).arg(qMax(f.h1, f.h2));
    if (f.h2 > 0 && f.h2 != f.h1)
        return QStringLiteral("%1 / %2 м").arg(f.h1).arg(f.h2);
    return h1;
}

QString MapController::coordinateText(double deg, bool latitude)
{
    const QChar hemi = latitude ? (deg < 0. ? QChar(u'S') : QChar(u'N'))
                                : (deg < 0. ? QChar(u'W') : QChar(u'E'));
    const double a = std::abs(deg);
    const int d = int(a);
    const double m = (a - d) * 60.;
    return QStringLiteral("%1 %2° %3′").arg(hemi).arg(d).arg(m, 0, 'f', 2);
}

void MapController::resetProcedures(void)
{
    if (m_map)
        m_map->hideAllProcedures();
    for (int k = TileManager::Sid; k <= TileManager::Approach; ++k) {
        m_names[k].clear();
        m_shown[k].clear();
    }
    m_runway.clear();
}

void MapController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void MapController::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}
