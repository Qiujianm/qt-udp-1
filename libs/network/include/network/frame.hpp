#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace network {

struct Frame {
    QJsonObject header;
    QByteArray payload;
};

QByteArray encodeFrame(const Frame& frame);
bool decodeFrame(const QByteArray& buffer, Frame* frame, QString* error = nullptr);

}  // namespace network
