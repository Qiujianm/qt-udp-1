#pragma once

#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QMap>
#include <QString>
#include <QHostAddress>

namespace console {

struct SubscriberInfo {
    QString clientId;
    QHostAddress clientIP;
    quint16 clientRtpPort;
    quint32 ssrc;
    QHostAddress consoleIP;
    quint16 consolePort;
    QWebSocket* socket;
};

class ConsoleControlServer : public QObject {
    Q_OBJECT
public:
    explicit ConsoleControlServer(quint16 port, QObject* parent = nullptr);
    ~ConsoleControlServer() override;

    bool start();
    void stop();
    quint16 port() const { return port_; }
    
    // 发送订阅消息到StreamClient
    void sendSubscribeToClient(const QString& clientId, const QHostAddress& clientIP, quint16 clientPort, 
                               const QHostAddress& consoleIP, quint16 consolePort, quint32 ssrc);
    void sendUnsubscribeToClient(const QString& clientId, const QHostAddress& clientIP, quint16 clientPort,
                                 const QHostAddress& consoleIP, quint16 consolePort, quint32 ssrc);

signals:
    void clientConnected(const QString& clientId, const QHostAddress& clientIP, quint16 clientRtpPort, quint32 ssrc);
    void clientDisconnected(const QString& clientId);

private slots:
    void handleNewConnection();
    void handleDisconnected();
    void handleTextMessage(const QString& message);

private:
    QWebSocketServer* server_;
    quint16 port_;
    QMap<QString, SubscriberInfo> clients_;  // clientId -> info
    QMap<QWebSocket*, QString> socketToClientId_;  // WebSocket -> clientId
};

}  // namespace console

