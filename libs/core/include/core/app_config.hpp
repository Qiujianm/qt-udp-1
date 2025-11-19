#pragma once

#include <QString>
#include <QUrl>

namespace core {

class AppConfig {
public:
    static AppConfig FromDefaults();
    static AppConfig FromFile(const QString& path);

    const QString& serverUrl() const noexcept;
    int reconnectIntervalMs() const noexcept;
    int keepAliveIntervalMs() const noexcept;
    quint16 listenPort() const noexcept;
    const QString& source() const noexcept;
    const QString& streamControlUrl() const noexcept;
    const QString& restApiUrl() const noexcept;
    quint16 restListenPort() const noexcept;
    const QString& licenseToken() const noexcept;
    const QString& licenseValidationUrl() const noexcept;
    const QString& telegramBotToken() const noexcept;
    bool telegramEnabled() const noexcept;
    QUrl websocketUrl(const QString& endpoint) const;

private:
    QString serverUrl_{"http://localhost:8000"};
    QString restApiUrl_{"http://127.0.0.1:8080"};
    int reconnectIntervalMs_{5000};
    int keepAliveIntervalMs_{10000};
    quint16 listenPort_{8765};
    quint16 restListenPort_{8080};
    QString streamControlUrl_{"ws://127.0.0.1:7000"};
    QString licenseToken_;
    QString licenseValidationUrl_;
    QString telegramBotToken_;
    bool telegramEnabled_{false};
    QString source_{"defaults"};
};

}  // namespace core
