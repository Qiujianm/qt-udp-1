#include "console/console_control_server.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace console {

ConsoleControlServer::ConsoleControlServer(quint16 port, QObject* parent)
    : QObject(parent), port_(port), server_(nullptr) {
}

ConsoleControlServer::~ConsoleControlServer() {
    stop();
}

bool ConsoleControlServer::start() {
    if (server_) {
        return true;  // Already started
    }

    server_ = new QWebSocketServer(QStringLiteral("DesktopConsole Control Server"),
                                    QWebSocketServer::NonSecureMode, this);
    
    if (!server_->listen(QHostAddress::AnyIPv4, port_)) {
        qWarning() << "[ConsoleControlServer] Failed to listen on port" << port_ << server_->errorString();
        delete server_;
        server_ = nullptr;
        return false;
    }

    connect(server_, &QWebSocketServer::newConnection, this, &ConsoleControlServer::handleNewConnection);
    qInfo() << "[ConsoleControlServer] Listening on port" << port_ << "for StreamClient connections";
    return true;
}

void ConsoleControlServer::stop() {
    if (server_) {
        server_->close();
        server_->deleteLater();
        server_ = nullptr;
    }
    clients_.clear();
    socketToClientId_.clear();
}

void ConsoleControlServer::sendSubscribeToClient(const QString& clientId, const QHostAddress& clientIP, quint16 clientPort,
                                                  const QHostAddress& consoleIP, quint16 consolePort, quint32 ssrc) {
    // 查找该客户端的WebSocket连接
    auto it = clients_.find(clientId);
    if (it == clients_.end() || !it->socket || it->socket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "[ConsoleControlServer] Client" << clientId << "not connected, cannot send subscribe";
        return;
    }

    QJsonObject obj{
        {QStringLiteral("action"), QStringLiteral("subscribe")},
        {QStringLiteral("client_id"), clientId},
        {QStringLiteral("ssrc"), static_cast<qint64>(ssrc)},
        {QStringLiteral("port"), static_cast<int>(consolePort)},
        {QStringLiteral("host"), consoleIP.toString()}
    };

    const QString message = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    it->socket->sendTextMessage(message);
    
    qInfo() << "[ConsoleControlServer] Sent subscribe to" << clientId << "at" << clientIP.toString() 
            << "console" << consoleIP.toString() << ":" << consolePort;
}

void ConsoleControlServer::sendUnsubscribeToClient(const QString& clientId, const QHostAddress& clientIP, quint16 clientPort,
                                                    const QHostAddress& consoleIP, quint16 consolePort, quint32 ssrc) {
    auto it = clients_.find(clientId);
    if (it == clients_.end() || !it->socket || it->socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QJsonObject obj{
        {QStringLiteral("action"), QStringLiteral("unsubscribe")},
        {QStringLiteral("client_id"), clientId},
        {QStringLiteral("ssrc"), static_cast<qint64>(ssrc)},
        {QStringLiteral("port"), static_cast<int>(consolePort)},
        {QStringLiteral("host"), consoleIP.toString()}
    };

    const QString message = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    it->socket->sendTextMessage(message);
    
    qInfo() << "[ConsoleControlServer] Sent unsubscribe to" << clientId;
}

void ConsoleControlServer::handleNewConnection() {
    QWebSocket* socket = server_->nextPendingConnection();
    if (!socket) {
        return;
    }

    QHostAddress peerAddress = socket->peerAddress();
    
    connect(socket, &QWebSocket::disconnected, this, &ConsoleControlServer::handleDisconnected);
    connect(socket, &QWebSocket::textMessageReceived, this, &ConsoleControlServer::handleTextMessage);
    
    qInfo() << "[ConsoleControlServer] New connection from" << peerAddress.toString();
}

void ConsoleControlServer::handleDisconnected() {
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket) {
        return;
    }

    QString clientId = socketToClientId_.value(socket);
    if (!clientId.isEmpty()) {
        clients_.remove(clientId);
        socketToClientId_.remove(socket);
        emit clientDisconnected(clientId);
        qInfo() << "[ConsoleControlServer] Client" << clientId << "disconnected";
    }
    
    socket->deleteLater();
}

void ConsoleControlServer::handleTextMessage(const QString& message) {
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket) {
        return;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "[ConsoleControlServer] Invalid JSON:" << error.errorString();
        return;
    }

    if (!doc.isObject()) {
        return;
    }

    const QJsonObject obj = doc.object();
    const QString action = obj.value(QStringLiteral("action")).toString();
    
    if (action == QStringLiteral("map")) {
        // StreamClient注册自己
        const QString clientId = obj.value(QStringLiteral("client_id")).toString();
        const quint32 ssrc = static_cast<quint32>(obj.value(QStringLiteral("ssrc")).toVariant().toULongLong());
        const quint16 rtpPort = static_cast<quint16>(obj.value(QStringLiteral("rtp_port")).toInt(5004));
        
        if (!clientId.isEmpty() && ssrc != 0) {
            SubscriberInfo info;
            info.clientId = clientId;
            info.clientIP = socket->peerAddress();
            info.clientRtpPort = rtpPort;
            info.ssrc = ssrc;
            info.socket = socket;
            
            clients_[clientId] = info;
            socketToClientId_[socket] = clientId;
            
            qInfo() << "[ConsoleControlServer] Client registered:" << clientId 
                    << "IP" << info.clientIP.toString() << "RTP port" << rtpPort << "SSRC" << ssrc;
            
            emit clientConnected(clientId, info.clientIP, rtpPort, ssrc);
        }
    }
}

}  // namespace console

