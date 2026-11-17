#ifndef TILEFORMAT_H
#define TILEFORMAT_H

/*
 * набор функций длля удобной работы с форматами тайлов
 * Рендер QByteArray->QImage ждет формат из списка плагинов qt
 */

#include <QByteArray>
#include <QImageReader>
#include <QList>

namespace mapGL {
namespace format {

inline QByteArray normalize(const QByteArray &fmt)
{
    return fmt.trimmed().toLower();
}

/* для разделения внутри декодера
 * Затем добавится поддержка векторов
 * для аэродромов, sid, star и тд
 */
inline bool isVector(const QByteArray &fmt)
{
    const QByteArray f = normalize(fmt);
    return f == "pbf" || f == "mvt";
}

inline bool isSupported(const QByteArray &fmt)
{
    static const QList<QByteArray> supported = QImageReader::supportedImageFormats();
    return supported.contains(normalize(fmt));
}

// "image/png; charset=binary" -> "png"
inline QByteArray fromMimeType(const QByteArray &contentType)
{
    const QByteArray mime = contentType.split(';').first().trimmed().toLower();
    if (mime.isEmpty())
        return QByteArray();
    const QList<QByteArray> fmts = QImageReader::imageFormatsForMimeType(mime);
    return fmts.isEmpty() ? QByteArray() : fmts.first();
}

} // namespace format
} // namespace mapGL

#endif // TILEFORMAT_H
