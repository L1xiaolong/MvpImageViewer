#pragma once
#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QVector>
#include <QtEndian>
#include <algorithm>

// A real uncompressed Bayer DNG, decoded through LibRaw rather than an injected
// cached plane. The deterministic samples let tests compare exact sensor values.
inline bool writeDngFixture(const QString& path, int width = 1536, int height = 1024) {
    struct Tag { quint16 id; quint16 type; quint32 count; QByteArray bytes; };
    auto words = [](std::initializer_list<quint16> values) {
        QByteArray data; QDataStream stream(&data, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        for (auto value : values) stream << value;
        return data;
    };
    auto longs = [](std::initializer_list<quint32> values) {
        QByteArray data; QDataStream stream(&data, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        for (auto value : values) stream << value;
        return data;
    };
    QVector<Tag> tags{
        {256, 4, 1, longs({quint32(width)})}, {257, 4, 1, longs({quint32(height)})},
        {258, 3, 1, words({16})}, {259, 3, 1, words({1})}, {262, 3, 1, words({32803})},
        {271, 2, 8, QByteArray("MvpView", 8)}, {272, 2, 8, QByteArray("Fixture", 8)},
        {273, 4, 1, longs({0})}, {274, 3, 1, words({1})}, {277, 3, 1, words({1})},
        {278, 4, 1, longs({quint32(height)})}, {279, 4, 1, longs({quint32(width * height * 2)})},
        {284, 3, 1, words({1})}, {33421, 3, 2, words({2, 2})},
        {33422, 1, 4, QByteArray::fromHex("00010102")},
        {50706, 1, 4, QByteArray::fromHex("01040000")},
        {50707, 1, 4, QByteArray::fromHex("01010000")},
        {50708, 2, 12, QByteArray("MvpView DNG", 12)},
        {50710, 1, 3, QByteArray::fromHex("000102")}, {50711, 3, 1, words({1})},
        {50714, 5, 1, longs({64, 1})}, {50717, 4, 1, longs({16383})},
        {50718, 5, 2, longs({1, 1, 1, 1})}, {50719, 4, 2, longs({0, 0})},
        {50720, 4, 2, longs({quint32(width), quint32(height)})},
        {50721, 10, 9, longs({1,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,1,1})},
        {50728, 5, 3, longs({1,1,1,1,1,1})}, {50778, 3, 1, words({21})},
        {50829, 4, 4, longs({0,0,quint32(height),quint32(width)})}
    };
    std::sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b) { return a.id < b.id; });
    const quint32 payloadStart = 8 + 2 + tags.size() * 12 + 4;
    quint32 pixelStart = payloadStart;
    for (const auto& tag : tags) if (tag.bytes.size() > 4)
        pixelStart += (tag.bytes.size() + 1) & ~1;
    QByteArray data, payload;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0x4949) << quint16(42) << quint32(8) << quint16(tags.size());
    for (auto& tag : tags) {
        if (tag.id == 273) tag.bytes = longs({pixelStart});
        stream << tag.id << tag.type << tag.count;
        if (tag.bytes.size() <= 4) {
            const QByteArray field = tag.bytes + QByteArray(4 - tag.bytes.size(), '\0');
            stream.writeRawData(field.constData(), 4);
        } else {
            stream << quint32(payloadStart + payload.size());
            payload += tag.bytes;
            if (payload.size() & 1) payload.append('\0');
        }
    }
    stream << quint32(0);
    data += payload;
    QByteArray pixels(width * height * 2, Qt::Uninitialized);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        qToLittleEndian<quint16>(quint16(64 + (x * 31 + y * 11) % 16000),
                                pixels.data() + (y * width + x) * 2);
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() &&
           file.write(pixels) == pixels.size();
}
