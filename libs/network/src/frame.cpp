#include "network/frame.hpp"

#include <QDataStream>
#include <QJsonDocument>
#include <QIODevice>

namespace network {

namespace {
constexpr quint32 kMagic = 0x51544652;  // 'QTFR'
constexpr quint16 kVersion = 1;
constexpr int kHeaderFieldsSize = sizeof(quint32) + sizeof(quint16) + sizeof(quint32);
}  // namespace

QByteArray encodeFrame(const Frame& frame) {
    const QJsonDocument doc(frame.header);
    const QByteArray headerBytes = doc.toJson(QJsonDocument::Compact);

    QByteArray buffer;
    buffer.reserve(kHeaderFieldsSize + headerBytes.size() + frame.payload.size());

    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << kMagic;
    stream << kVersion;
    stream << static_cast<quint32>(headerBytes.size());
    stream.writeRawData(headerBytes.constData(), headerBytes.size());
    stream.writeRawData(frame.payload.constData(), frame.payload.size());

    return buffer;
}

bool decodeFrame(const QByteArray& buffer, Frame* frame, QString* error) {
    if (!frame) {
        if (error) {
            *error = QStringLiteral("Frame pointer is null");
        }
        return false;
    }

    if (buffer.size() < kHeaderFieldsSize) {
        if (error) {
            *error = QStringLiteral("Frame too small");
        }
        return false;
    }

    QDataStream stream(buffer);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 magic = 0;
    quint16 version = 0;
    quint32 headerSize = 0;

    stream >> magic;
    stream >> version;
    stream >> headerSize;

    if (magic != kMagic) {
        if (error) {
            *error = QStringLiteral("Invalid frame magic");
        }
        return false;
    }

    if (version != kVersion) {
        if (error) {
            *error = QStringLiteral("Unsupported frame version %1").arg(version);
        }
        return false;
    }

    const int headerOffset = kHeaderFieldsSize;
    if (buffer.size() < headerOffset + static_cast<int>(headerSize)) {
        if (error) {
            *error = QStringLiteral("Incomplete frame data");
        }
        return false;
    }

    const QByteArray headerBytes = buffer.mid(headerOffset, headerSize);
    const QByteArray payloadBytes = buffer.mid(headerOffset + headerSize);

    const QJsonDocument doc = QJsonDocument::fromJson(headerBytes);
    if (!doc.isObject()) {
        if (error) {
            *error = QStringLiteral("Invalid header JSON");
        }
        return false;
    }

    frame->header = doc.object();
    frame->payload = payloadBytes;
    return true;
}

}  // namespace network
