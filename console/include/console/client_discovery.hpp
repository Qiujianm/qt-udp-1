#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QHash>
#include <QString>

namespace console {

struct DiscoveredClient {
    QString clientId;
    QString ip;
    quint16 rtpPort{5004};
    quint16 controlPort{7000};
    quint32 ssrc{0};
    QString controlUrl;
    qint64 lastSeen{0};  // 时间戳
};

class ClientDiscovery : public QObject {
    Q_OBJECT
public:
    explicit ClientDiscovery(QObject* parent = nullptr);
    ~ClientDiscovery() override;

    void start(quint16 listenPort = 40810);
    void stop();
    
    QList<DiscoveredClient> discoveredClients() const;
    DiscoveredClient client(const QString& clientId) const;

signals:
    void clientDiscovered(const DiscoveredClient& client);
    void clientUpdated(const DiscoveredClient& client);
    void clientExpired(const QString& clientId);

private slots:
    void handleReadyRead();
    void checkExpiredClients();

private:
    QUdpSocket socket_;
    QTimer expireTimer_;
    QHash<QString, DiscoveredClient> clients_;
    static constexpr qint64 kClientTimeoutMs = 5000;  // 5秒超时
};

}  // namespace console

