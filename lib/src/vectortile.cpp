#include "vectortile.h"

#include <QStringList>
#include <cstring>
#include <zlib.h>

namespace mapGL {
namespace mvt {

namespace {

constexpr qsizetype k_maxUnpackedSize = 16 * 1024 * 1024;

enum WireType {
    Varint          = 0,
    Fixed64         = 1,
    LengthDelimited = 2,
    Fixed32         = 5
};

enum GeomCommand {
    MoveTo    = 1,
    LineTo    = 2,
    ClosePath = 7
};

class PbfReader
{
public:
    explicit PbfReader(const QByteArray &data)
        : m_p(reinterpret_cast<const uchar *>(data.constData()))
        , m_end(m_p + data.size()) {}

    bool atEnd() const { return m_p >= m_end; }
    bool ok() const { return m_ok; }
    quint32 field() const { return m_field; }
    int wire() const { return m_wire; }

    bool next()
    {
        if (!m_ok || atEnd())
            return false;
        const quint64 key = varint();
        m_field = static_cast<quint32>(key >> 3);
        m_wire = static_cast<int>(key & 0x7);
        return m_ok;
    }

    quint64 varint()
    {
        quint64 result = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (atEnd()) {
                m_ok = false;
                return 0;
            }
            const uchar byte = *m_p++;
            result |= quint64(byte & 0x7f) << shift;
            if (!(byte & 0x80))
                return result;
        }
        m_ok = false;
        return 0;
    }

    QByteArray bytes()
    {
        const quint64 len = varint();
        if (!m_ok || len > quint64(m_end - m_p)) {
            m_ok = false;
            return QByteArray();
        }
        const QByteArray out = QByteArray::fromRawData(reinterpret_cast<const char *>(m_p),
                                                       static_cast<qsizetype>(len));
        m_p += len;
        return out;
    }

    double fixed64()
    {
        double v = 0.;
        if (take(&v, sizeof(v)))
            return v;
        return 0.;
    }

    float fixed32()
    {
        float v = 0.f;
        if (take(&v, sizeof(v)))
            return v;
        return 0.f;
    }

    void skip()
    {
        switch (m_wire) {
        case Varint:          varint(); break;
        case Fixed64:         advance(8); break;
        case LengthDelimited: bytes(); break;
        case Fixed32:         advance(4); break;
        default:              m_ok = false; break;
        }
    }

private:
    bool take(void *dst, qsizetype n)
    {
        if (m_end - m_p < n) {
            m_ok = false;
            return false;
        }
        std::memcpy(dst, m_p, static_cast<size_t>(n));
        m_p += n;
        return true;
    }

    void advance(qsizetype n)
    {
        if (m_end - m_p < n) {
            m_ok = false;
            return;
        }
        m_p += n;
    }

    const uchar *m_p;
    const uchar *m_end;
    quint32 m_field = 0;
    int m_wire = 0;
    bool m_ok = true;
};

inline qint64 zigzag(quint64 v)
{
    return static_cast<qint64>(v >> 1) ^ -static_cast<qint64>(v & 1);
}

bool readPacked(const QByteArray &data, QVector<quint32> *out)
{
    PbfReader r(data);
    while (!r.atEnd()) {
        const quint64 v = r.varint();
        if (!r.ok())
            return false;
        out->push_back(static_cast<quint32>(v));
    }
    return true;
}

QVariant readValue(const QByteArray &data)
{
    PbfReader r(data);
    QVariant value;
    while (r.next()) {
        switch (r.field()) {
        case 1: value = QString::fromUtf8(r.bytes()); break;
        case 2: value = static_cast<double>(r.fixed32()); break;
        case 3: value = r.fixed64(); break;
        case 4: value = static_cast<qint64>(r.varint()); break;
        case 5: value = static_cast<quint64>(r.varint()); break;
        case 6: value = zigzag(r.varint()); break;
        case 7: value = r.varint() != 0; break;
        default: r.skip(); break;
        }
    }
    return r.ok() ? value : QVariant();
}

bool decodeGeometry(const QVector<quint32> &cmds, GeomType type, QVector<QPolygonF> *parts)
{
    qint64 x = 0;
    qint64 y = 0;
    QPolygonF current;

    auto flush = [&]() {
        if (!current.isEmpty()) {
            parts->push_back(current);
            current.clear();
        }
    };

    qsizetype i = 0;
    while (i < cmds.size()) {
        const quint32 cmdInt = cmds[i++];
        const quint32 id = cmdInt & 0x7;
        const quint32 count = cmdInt >> 3;

        if (id == MoveTo || id == LineTo) {
            if (qsizetype(count) * 2 > cmds.size() - i)
                return false;
            for (quint32 n = 0; n < count; ++n) {
                x += zigzag(cmds[i++]);
                y += zigzag(cmds[i++]);
                if (id == MoveTo && (type != GeomType::Point || n == 0))
                    flush();
                current.push_back(QPointF(double(x), double(y)));
                if (id == MoveTo && type == GeomType::Point)
                    flush();
            }
        } else if (id == ClosePath) {
            if (!current.isEmpty() && current.first() != current.last())
                current.push_back(current.first());
            flush();
        } else {
            return false;
        }
    }
    flush();
    return true;
}

bool readFeature(const QByteArray &data, const QStringList &keys,
                 const QVector<QVariant> &values, Feature *feature)
{
    PbfReader r(data);
    QVector<quint32> tags;
    QVector<quint32> geometry;

    while (r.next()) {
        switch (r.field()) {
        case 2:
            if (!readPacked(r.bytes(), &tags))
                return false;
            break;
        case 3:
            feature->type = static_cast<GeomType>(r.varint());
            break;
        case 4:
            if (!readPacked(r.bytes(), &geometry))
                return false;
            break;
        default:
            r.skip();
            break;
        }
    }
    if (!r.ok())
        return false;

    for (qsizetype t = 0; t + 1 < tags.size(); t += 2) {
        const quint32 k = tags[t];
        const quint32 v = tags[t + 1];
        if (k < quint32(keys.size()) && v < quint32(values.size()))
            feature->properties.insert(keys[k], values[v]);
    }

    return decodeGeometry(geometry, feature->type, &feature->parts);
}

bool readLayer(const QByteArray &data, Layer *layer)
{
    PbfReader r(data);
    QStringList keys;
    QVector<QVariant> values;
    QVector<QByteArray> rawFeatures;

    while (r.next()) {
        switch (r.field()) {
        case 1:  layer->name = QString::fromUtf8(r.bytes()); break;
        case 2:  rawFeatures.push_back(r.bytes()); break;
        case 3:  keys.push_back(QString::fromUtf8(r.bytes())); break;
        case 4:  values.push_back(readValue(r.bytes())); break;
        case 5:  layer->extent = static_cast<quint32>(r.varint()); break;
        default: r.skip(); break;
        }
    }
    if (!r.ok() || layer->extent == 0)
        return false;

    layer->features.reserve(rawFeatures.size());
    for (const QByteArray &raw : std::as_const(rawFeatures)) {
        Feature feature;
        if (readFeature(raw, keys, values, &feature) && !feature.parts.isEmpty())
            layer->features.push_back(std::move(feature));
    }
    return true;
}

} // namespace

QByteArray gunzip(const QByteArray &data)
{
    if (data.size() < 2)
        return data;

    const uchar b0 = static_cast<uchar>(data[0]);
    const uchar b1 = static_cast<uchar>(data[1]);
    const bool gzip = (b0 == 0x1f && b1 == 0x8b);
    const bool zlibHeader = (b0 == 0x78);
    if (!gzip && !zlibHeader)
        return data;

    z_stream stream{};
    if (inflateInit2(&stream, MAX_WBITS + 32) != Z_OK)
        return QByteArray();

    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = static_cast<uInt>(data.size());

    QByteArray out;
    char chunk[32 * 1024];
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        stream.next_out = reinterpret_cast<Bytef *>(chunk);
        stream.avail_out = sizeof(chunk);
        ret = inflate(&stream, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&stream);
            return QByteArray();
        }
        out.append(chunk, static_cast<qsizetype>(sizeof(chunk) - stream.avail_out));
        const bool truncated = (ret == Z_OK && stream.avail_in == 0 && stream.avail_out != 0);
        if (truncated || out.size() > k_maxUnpackedSize) {
            inflateEnd(&stream);
            return QByteArray();
        }
    }
    inflateEnd(&stream);
    return out;
}

bool parse(const QByteArray &pbf, QVector<Layer> *layers)
{
    PbfReader r(pbf);
    while (r.next()) {
        if (r.field() == 3 && r.wire() == LengthDelimited) {
            Layer layer;
            if (readLayer(r.bytes(), &layer))
                layers->push_back(std::move(layer));
        } else {
            r.skip();
        }
    }
    return r.ok();
}

} // namespace mvt
} // namespace mapGL
