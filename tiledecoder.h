#ifndef TILEDECODER_H
#define TILEDECODER_H

/*
 * класс рендера QByteArray->QImage
 * в отдельном пуле потоков
 *
 * операция достаточно дорогостоящая,
 * поэтому не должна блокировать основной поток
 */

#include "mapGL.h"

#include <QImage>
#include <QObject>
#include <QThreadPool>

class TileDecoder : public QObject
{
    Q_OBJECT
public:
    explicit TileDecoder(QObject *parent = nullptr);
    ~TileDecoder() override;

    void decode(const mapGL::TileMeta &tile);

    // потокобезопасно
    static QImage decodeImage(const QByteArray &data, const QByteArray &format);

signals:
    void decoded(uint64_t key, const QImage &img, mapGL::TileQuality quality);

private:
    QThreadPool m_pool;
};

#endif // TILEDECODER_H
