#include "console/console_broadcaster.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QDateTime>
#include <QDebug>

namespace console {

namespace {
constexpr quint16 kBroadcastPort = 40811;  // DesktopConsole广播端口（与StreamClient监听端口不同）
constexpr char kBroadcastType[] = "qt_desktop_suite_console";
}  // namespace

ConsoleBroadcaster::ConsoleBroadcaster(quint16 controlPort, QObject* parent)
    : QObject(parent), controlPort_(controlPort) {
}

ConsoleBroadcaster::~ConsoleBroadcaster() {
    stop();
}

void ConsoleBroadcaster::setControlPort(quint16 port) {
    controlPort_ = port;
}

void ConsoleBroadcaster::start(int intervalMs) {
    if (timer_.isActive()) {
        return;
    }
    
    timer_.setInterval(qMax(1000, intervalMs));
    connect(&timer_, &QTimer::timeout, this, &ConsoleBroadcaster::sendBroadcast);
    timer_.start();
    sendBroadcast();  // 立即发送一次
    qInfo() << "[ConsoleBroadcaster] Started broadcasting console control server on port" << controlPort_;
}

void ConsoleBroadcaster::stop() {
    timer_.stop();
    if (socket_.isOpen()) {
        socket_.close();
    }
    qInfo() << "[ConsoleBroadcaster] Stopped broadcasting";
}

void ConsoleBroadcaster::sendBroadcast() {
    const QString localIP = resolveLocalIP();
    if (localIP.isEmpty()) {
        qWarning() << "[ConsoleBroadcaster] Cannot resolve local IP, skipping broadcast";
        return;
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QString::fromUtf8(kBroadcastType));
    obj.insert(QStringLiteral("control_port"), static_cast<int>(controlPort_));
    obj.insert(QStringLiteral("control_url"), QStringLiteral("ws://%1:%2").arg(localIP).arg(controlPort_));
    obj.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    const QJsonDocument doc(obj);
    const QByteArray payload = doc.toJson(QJsonDocument::Compact);

    // 广播到所有网络接口
    QSet<QHostAddress> targets;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || 
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback() || ip.isMulticast()) {
                continue;
            }
            const QHostAddress broadcast = entry.broadcast();
            if (!broadcast.isNull()) {
                targets.insert(broadcast);
            }
        }
    }
    targets.insert(QHostAddress::Broadcast);  // 也发送到通用广播地址

    for (const QHostAddress& target : targets) {
        const qint64 written = socket_.writeDatagram(payload, target, kBroadcastPort);
        if (written == -1) {
            qWarning() << "[ConsoleBroadcaster] Failed to send broadcast to" << target.toString()
                       << "error" << socket_.errorString();
        }
    }
}

QString ConsoleBroadcaster::resolveLocalIP() const {
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || 
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol && !ip.isLoopback() && !ip.isMulticast()) {
                return ip.toString();
            }
        }
    }
    return QString();
}

}  // namespace console

