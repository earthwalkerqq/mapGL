#include "tiledecoder.h"

#include <QBuffer>
#include <QImageReader>
#include <QThread>

static constexpr int k_MaxThreads = 4;

TileDecoder::TileDecoder(QObject *parent)
    : QObject(parent)
{
    // один поток на GUI
    m_pool.setMaxThreadCount(qBound(1, QThread::idealThreadCount() - 1, k_MaxThreads));
}

TileDecoder::~TileDecoder()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void TileDecoder::decode(const mapGL::TileMeta &tile)
{
    m_pool.start([this, tile] {
        QImage img = decodeImage(tile.rwTile.data, tile.rwTile.format);
        // назад в главный поток объекта
        QMetaObject::invokeMethod(this,
            [this, key = tile.key, img = std::move(img), q = tile.rwTile.quality] {
                emit decoded(key, img, q);
            },
            Qt::QueuedConnection);
    });
}

QImage TileDecoder::decodeImage(const QByteArray &data, const QByteArray &format)
{
    if (data.isEmpty())
        return QImage();

    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);

    QImageReader reader(&buffer, format);
    QImage img;
    if (!reader.read(&img)) {
        if (format.isEmpty())
            return QImage();
        buffer.seek(0);
        QImageReader autodetect(&buffer);
        if (!autodetect.read(&img))
            return QImage();
    }

    /*
     * Format_ARGB32_Premultiplied - цветовые каналы умножены на прозрачность
     * Format_RGB32 - канал прозрачности всегда заполнен значением 0xff
     * в процессе рендеринга игнорируется расчет смешивания цветов
     */
    const QImage::Format target = img.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied
                                                        : QImage::Format_RGB32;
    if (img.format() != target)
        img = img.convertToFormat(target);
    return img;
}
