#include "aerodromtilerasterprovider.h"
#include "tileformat.h"

AerodromTileRasterProvider::AerodromTileRasterProvider(const QString &icao, const QString &trg, QObject *parent)
    : TileStorage(trg, mapGL::k_maxTileZoom, parent), m_icao(icao) {}

void AerodromTileRasterProvider::prepare(void)
{
    const QList<Metadata> rows = m_db.from<Metadata>()
    .select(MetadataFields::name, MetadataFields::value)
        .exec();

    for (const Metadata &row : rows) {
        bool ok = false;
        if (row.name == QLatin1String("minzoom")) {
            const int v = row.value.toInt(&ok);
            if (ok) m_minzoom = v;
        } else if (row.name == QLatin1String("maxzoom")) {
            const int v = row.value.toInt(&ok);
            if (ok) m_maxzoom = v;
        } else if (row.name == QLatin1String("format")) {
            m_format = mapGL::format::normalize(row.value.toLatin1());
        } else if (row.name == QLatin1String("bounds")) {
            const QStringList parts = row.value.split(QLatin1Char(','));
            if (parts.size() == 4) {
                double v[4];
                bool all = true;
                for (int i = 0; i < 4 && all; ++i) {
                    v[i] = parts[i].trimmed().toDouble(&all);
                }
                if (all && v[0] < v[2] && v[1] < v[3]) {
                    m_minLon = v[0];
                    m_minLat = v[1];
                    m_maxLon = v[2];
                    m_maxLat = v[3];
                    m_hasBounds = true;
                }
            }
        }
    }

    /*
     * в данной версии ПО карта подложка имеет растовый
     * формат и загрузка векторов не допустима,
     * иначе упадет позже, на этапе рендеринга
     */
    if (mapGL::format::isVector(m_format)) {
        m_err = true;
        m_lastError = QStringLiteral("%1: векторные тайлы (%2) этим провайдером не поддерживаются")
                          .arg(m_target, QString::fromLatin1(m_format));
    } else if (!m_format.isEmpty() && !mapGL::format::isSupported(m_format)) {
        m_err = true;
        m_lastError = QStringLiteral("%1: нет плагина Qt для формата %2")
                          .arg(m_target, QString::fromLatin1(m_format));
    }
    if (m_err) {
        mapGL::warning(m_lastError);
    }
}
