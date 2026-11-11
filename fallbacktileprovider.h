#ifndef FALLBACKTILEPROVIDER_H
#define FALLBACKTILEPROVIDER_H

/*
 * Загрузка тайлов из подложки (z0-6)
 */

#include "tileprovider.h"
#include "orm.h"
#include "mapGL.h"

#include <QString>


class FallbackTileProvider : public TileProvider
{
public:
    explicit FallbackTileProvider(const QString trg, QObject *parent = nullptr)
        : m_target(trg), m_db(m_target), TileProvider{parent}
    {
        if (!m_db.connect()) {
            m_err = true;
            m_lastError = m_db.lastError();
            mapGL::warning(m_lastError);
        } else {
            prepare();
        }
    }
    FallbackTileProvider(const FallbackTileProvider &) = delete;
    FallbackTileProvider &operator=(const FallbackTileProvider &) = delete;

    void load(void) override;
    mapGL::TileQuality quality(void) override { return mapGL::TileQuality::Fallback; }
private:
    void prepare();

    QString m_target;
    DataBase m_db;

    bool m_fill_meta = false;
    int m_minzoom;
    int m_maxzoom;
    bool m_err = false;
    QString m_lastError;

    QSet<uint64_t> m_set_miss; // тайлы, которых нет в подложке
};

#endif // FALLBACKTILEPROVIDER_H
