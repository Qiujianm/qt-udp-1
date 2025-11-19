#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QString>
#include <QHostAddress>

namespace console {

class ConsoleBroadcaster : public QObject {
    Q_OBJECT
public:
    explicit ConsoleBroadcaster(quint16 controlPort, QObject* parent = nullptr);
    ~ConsoleBroadcaster() override;

    void start(int intervalMs = 2000);
    void stop();
    void setControlPort(quint16 port);

private slots:
    void sendBroadcast();

private:
    QUdpSocket socket_;
    QTimer timer_;
    quint16 controlPort_{7000};
    QString resolveLocalIP() const;
};

}  // namespace console

