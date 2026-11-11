#include "tilescheduler.h"

#include <QThread>


void TileScheduler::asyncTileLoad()
{
    for (const std::shared_ptr<TileProvider> &src : std::as_const(m_source))
    {
        QThread *thread = new QThread(this);
        src->moveToThread(thread);

        connect(src.get(), &TileProvider::readyTile, this, [this](mapGL::TileMeta rs) {
            emit readyTile(rs);
        });
        connect(src.get(), &TileProvider::failedTile, this, [](uint64_t &key, QString &msg) {
            uint8_t z;
            uint32_t x, y;
            mapGL::math::parseKey(key, &z, &x, &y);
            mapGL::warning(QStringLiteral("Error for key %1:%2:%3 - %4")
                .arg(z).arg(x).arg(y).arg(msg));
        });
        connect(thread, &QThread::started, src.get(), &TileProvider::load);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);

        thread->start();
    }
}
