#include "aerodromtilerasterprovider.h"
#include "standartsourcepath.h"
#include "tileformat.h"

AerodromTileRasterProvider::AerodromTileRasterProvider(const QString &icao, QObject *parent)
    : TileStorage(mapGL::aeroDB(icao), mapGL::k_maxTileZoom, parent), m_icao(icao) {}

void AerodromTileRasterProvider::prepare(void)
{
    readMetadata();

    if (mapGL::format::isVector(m_format)) {
        setError(QStringLiteral("%1: векторные тайлы (%2) этим провайдером не поддерживаются")
                     .arg(m_target, QString::fromLatin1(m_format)));
    } else if (!m_format.isEmpty() && !mapGL::format::isSupported(m_format)) {
        setError(QStringLiteral("%1: нет плагина Qt для формата %2")
                     .arg(m_target, QString::fromLatin1(m_format)));
    }
}

void AerodromTileRasterProvider::load(const QVector<uint64_t> &keys, quint64 generation)
{
    if (m_err || isStale(generation)) {
        return;
    }

    for (const uint64_t key : storageKeys(keys)) {
        if (isStale(generation)) {
            return;
        }
        if (m_set_miss.contains(key) || alreadyHaveRaster(key)) {
            continue;
        }

        QByteArray data;
        if (readTile(key, &data)) {
            emit readyTileRaster(mapGL::TileMeta{key, mapGL::RawTile{data, quality(), m_format}});
        }
    }
}
