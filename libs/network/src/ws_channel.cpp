#include "network/ws_channel.hpp"

#include <QDebug>

#include "network/frame.hpp"

namespace network {

WsChannel::WsChannel(QObject* parent) : QObject(parent) {
    connect(&socket_, &QWebSocket::connected, this, &WsChannel::handleConnected);
    connect(&socket_, &QWebSocket::disconnected, this, &WsChannel::handleDisconnected);
    connect(&socket_, &QWebSocket::binaryMessageReceived, this, &WsChannel::handleBinaryMessage);
        connect(&socket_, &QWebSocket::textMessageReceived, this, &WsChannel::handleTextMessage);
    connect(&socket_, &QWebSocket::errorOccurred, this, &WsChannel::handleError);
}

void WsChannel::connectTo(const QUrl& url) {
    socket_.open(url);
}

void WsChannel::disconnectFromServer() {
    socket_.close();
}

void WsChannel::sendFrame(const Frame& frame) {
    socket_.sendBinaryMessage(encodeFrame(frame));
}

    void WsChannel::sendText(const QString& message) {
        if (socket_.state() != QAbstractSocket::ConnectedState) {
            qWarning() << "[WsChannel] ✗ Cannot send text message, socket not connected (state:" << socket_.state() << ")";
            return;
        }
        qInfo() << "[WsChannel] Sending text message, size:" << message.size() << "bytes, state:" << socket_.state();
        socket_.sendTextMessage(message);
        qInfo() << "[WsChannel] ✓ Text message sent successfully";
    }

void WsChannel::handleConnected() {
    emit connected();
}

void WsChannel::handleDisconnected() {
    emit disconnected();
}

void WsChannel::handleBinaryMessage(const QByteArray& payload) {
    emit binaryMessageReceived(payload);

    // 尝试解码为Frame消息（用于控制消息）
    // 如果失败，可能是其他类型的二进制数据（如截图），这是正常的，不记录警告
    Frame frame;
    QString error;
    if (decodeFrame(payload, &frame, &error)) {
        emit frameReceived(frame);
    } else {
        // 降级为DEBUG：截图等二进制数据不是Frame格式，这是正常的
        qDebug() << "[WsChannel] Binary data is not a Frame message (this is normal for screenshots):" << error;
    }
}

    void WsChannel::handleTextMessage(const QString& message) {
        emit textMessageReceived(message);
}

void WsChannel::handleError(QAbstractSocket::SocketError error) {
    emit errorOccurred(error, socket_.errorString());
}

bool WsChannel::isConnected() const {
    return socket_.state() == QAbstractSocket::ConnectedState;
}

}  // namespace network
