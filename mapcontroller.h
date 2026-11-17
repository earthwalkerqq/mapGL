#ifndef MAPCONTROLLER_H
#define MAPCONTROLLER_H

#include "tilemanager.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class MapController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TileManager *map READ map WRITE setMap NOTIFY mapChanged)
    Q_PROPERTY(QString icao READ icao NOTIFY aerodromeChanged)
    Q_PROPERTY(QStringList runways READ runways NOTIFY aerodromeChanged)
    Q_PROPERTY(QString runway READ runway NOTIFY proceduresChanged)
    Q_PROPERTY(QStringList sidNames READ sidNames NOTIFY proceduresChanged)
    Q_PROPERTY(QStringList starNames READ starNames NOTIFY proceduresChanged)
    Q_PROPERTY(QStringList approachNames READ approachNames NOTIFY proceduresChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit MapController(QObject *parent = nullptr);

    TileManager *map(void) const { return m_map; }
    void setMap(TileManager *map);

    QString icao(void) const { return m_icao; }
    QStringList runways(void) const { return m_runways; }
    QString runway(void) const { return m_runway; }
    QStringList sidNames(void) const { return m_names[TileManager::Sid]; }
    QStringList starNames(void) const { return m_names[TileManager::Star]; }
    QStringList approachNames(void) const { return m_names[TileManager::Approach]; }
    bool busy(void) const { return m_busy; }
    QString status(void) const { return m_status; }

    Q_INVOKABLE void findAerodrome(const QString &icao);
    Q_INVOKABLE void selectRunway(const QString &runway);
    Q_INVOKABLE void showSid(const QString &route);
    Q_INVOKABLE void showStar(const QString &route);
    Q_INVOKABLE void showApproach(const QString &route);
    Q_INVOKABLE QVariantMap fixAt(qreal x, qreal y) const;

signals:
    void mapChanged(void);
    void aerodromeChanged(void);
    void proceduresChanged(void);
    void busyChanged(void);
    void statusChanged(void);

private:
    struct AerodromeInfo {
        bool ok = false;
        QString error;
        QString icao;
        double lat = 0.;
        double lon = 0.;
        QStringList runways;
    };

    static AerodromeInfo readAerodrome(const QString &icao);
    static QString altitudeText(const mapGL::proc::Fix &fix);
    static QString coordinateText(double deg, bool latitude);

    void onAerodromeRead(const AerodromeInfo &info);
    void onProceduresReady(const QString &icao, const QString &runway);
    void onProceduresFailed(const QString &icao, const QString &runway, const QString &error);
    void showRoute(int kind, const QString &route);
    void resetProcedures(void);
    void setBusy(bool busy);
    void setStatus(const QString &status);

    static constexpr double k_aerodromeZoom = 10.;

    QPointer<TileManager> m_map;

    QString m_icao;
    QStringList m_runways;
    QString m_runway;
    QStringList m_names[3];
    QString m_shown[3];

    bool m_busy = false;
    QString m_status;
    quint64 m_request = 0;
};

#endif // MAPCONTROLLER_H
