#ifndef FALLBACKTILEPROVIDER_H
#define FALLBACKTILEPROVIDER_H

/*
 * Загрузка тайлов из подложки (z0-6)
 */

#include "tileprovider.h"
#include "orm.h"
#include "mapGL.h"

#include <QSet>
#include <QString>


class FallbackTileProvider : public TileProvider
{
    Q_OBJECT
public:
    /*
     * В конструкторе только запоминаем путь: он выполняется в ГЛАВНОМ потоке.
     * БД открывается в init() — уже в потоке провайдера: QSqlDatabase можно
     * использовать только в том потоке, где соединение создано.
     */
    explicit FallbackTileProvider(const QString &trg, QObject *parent = nullptr)
        : TileProvider{parent}, m_target(trg), m_db(m_target) {}
    FallbackTileProvider(const FallbackTileProvider &) = delete;
    FallbackTileProvider &operator=(const FallbackTileProvider &) = delete;

    void init(void) override;

    void load(const QVector<uint64_t> &keys, quint64 generation) override;
    mapGL::TileQuality quality(void) const override { return mapGL::TileQuality::Fallback; }
    // SQLite блокирующий — читаем в отдельном потоке, не в главном
    bool needsOwnThread(void) const override { return true; }
private:
    void prepare();

    QString m_target;
    DataBase m_db;

    int m_minzoom = -1; // -1 — в metadata не указан
    int m_maxzoom = -1;
    QByteArray m_format; // для декодера; пусто — в metadata не указан (см. tileformat.h)
    /*
     * TODO: сделать bounds
     * и в зависимости от того,
     * попадает ли наша камера в этот
     * bound делать запрос в бд
     */

    bool m_err = false;
    QString m_lastError;

    QSet<uint64_t> m_set_miss; // тайлы, которых нет в подложке
};

#endif // FALLBACKTILEPROVIDER_H
