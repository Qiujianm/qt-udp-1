#pragma once

#include <QObject>
#include <QUrl>
#include <QAbstractSocket>
#include <QWebSocket>

#include "network/frame.hpp"

namespace network {

class WsChannel final : public QObject {
    Q_OBJECT
public:
    explicit WsChannel(QObject* parent = nullptr);

    void connectTo(const QUrl& url);
    void disconnectFromServer();
    void sendFrame(const Frame& frame);
        void sendText(const QString& message);
    bool isConnected() const;

signals:
    void connected();
    void disconnected();
    void binaryMessageReceived(const QByteArray& payload);
    void frameReceived(const Frame& frame);
        void textMessageReceived(const QString& text);
    void errorOccurred(QAbstractSocket::SocketError code, const QString& description);

private slots:
    void handleConnected();
    void handleDisconnected();
    void handleBinaryMessage(const QByteArray& payload);
        void handleTextMessage(const QString& message);
    void handleError(QAbstractSocket::SocketError error);

private:
    QWebSocket socket_;
};

}  // namespace network
