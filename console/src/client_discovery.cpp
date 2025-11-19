#include "console/client_discovery.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QDebug>

namespace console {

namespace {
constexpr char kBroadcastType[] = "qt_desktop_suite_stream_client";
}  // namespace

ClientDiscovery::ClientDiscovery(QObject* parent)
    : QObject(parent) {
    expireTimer_.setInterval(1000);  // 每秒检查一次
    connect(&expireTimer_, &QTimer::timeout, this, &ClientDiscovery::checkExpiredClients);
}

ClientDiscovery::~ClientDiscovery() {
    stop();
}

void ClientDiscovery::start(quint16 listenPort) {
    if (socket_.isOpen()) {
        return;
    }
    
    if (!socket_.bind(QHostAddress::AnyIPv4, listenPort, 
                     QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning() << "[ClientDiscovery] Failed to bind port" << listenPort << socket_.errorString();
        return;
    }
    
    connect(&socket_, &QUdpSocket::readyRead, this, &ClientDiscovery::handleReadyRead);
    expireTimer_.start();
    qInfo() << "[ClientDiscovery] Started listening on port" << listenPort;
}

void ClientDiscovery::stop() {
    expireTimer_.stop();
    if (socket_.isOpen()) {
        socket_.close();
    }
    clients_.clear();
}

QList<DiscoveredClient> ClientDiscovery::discoveredClients() const {
    return clients_.values();
}

DiscoveredClient ClientDiscovery::client(const QString& clientId) const {
    return clients_.value(clientId);
}

void ClientDiscovery::handleReadyRead() {
    while (socket_.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(socket_.pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        if (socket_.readDatagram(datagram.data(), datagram.size(), &sender, &senderPort) <= 0) {
            continue;
        }

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(datagram, &error);
        if (error.error != QJsonParseError::NoError) {
            continue;
        }

        if (!doc.isObject()) {
            continue;
        }

        const QJsonObject obj = doc.object();
        if (obj.value(QStringLiteral("type")).toString() != QString::fromUtf8(kBroadcastType)) {
            continue;
        }

        DiscoveredClient client;
        client.clientId = obj.value(QStringLiteral("client_id")).toString();
        client.ip = obj.value(QStringLiteral("ip")).toString();
        if (client.ip.isEmpty()) {
            client.ip = sender.toString();
        }
        client.rtpPort = static_cast<quint16>(obj.value(QStringLiteral("rtp_port")).toInt());
        client.controlPort = static_cast<quint16>(obj.value(QStringLiteral("control_port")).toInt());
        client.ssrc = static_cast<quint32>(obj.value(QStringLiteral("ssrc")).toVariant().toULongLong());
        client.controlUrl = obj.value(QStringLiteral("control_url")).toString();
        if (client.controlUrl.isEmpty() && !client.ip.isEmpty() && client.controlPort != 0) {
            client.controlUrl = QStringLiteral("ws://%1:%2").arg(client.ip).arg(client.controlPort);
        }
        client.lastSeen = QDateTime::currentMSecsSinceEpoch();

        if (client.clientId.isEmpty() || client.ssrc == 0) {
            continue;
        }

        const bool isNew = !clients_.contains(client.clientId);
        clients_[client.clientId] = client;

        if (isNew) {
            qInfo() << "[ClientDiscovery] Discovered client" << client.clientId 
                    << "at" << client.ip << "RTP port" << client.rtpPort;
            emit clientDiscovered(client);
        } else {
            emit clientUpdated(client);
        }
    }
}

void ClientDiscovery::checkExpiredClients() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList expired;
    
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if (now - it->lastSeen > kClientTimeoutMs) {
            expired.append(it.key());
        }
    }
    
    for (const QString& clientId : expired) {
        qInfo() << "[ClientDiscovery] Client" << clientId << "expired";
        clients_.remove(clientId);
        emit clientExpired(clientId);
    }
}

}  // namespace console

