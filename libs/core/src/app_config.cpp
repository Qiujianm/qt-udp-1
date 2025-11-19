#include "core/app_config.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

#include <utility>

namespace core {

namespace {
QString readStringOrDefault(const QJsonObject& obj, const char* key, const QString& fallback) {
    const auto value = obj.value(QLatin1String(key));
    if (value.isString()) {
        const auto str = value.toString().trimmed();
        if (!str.isEmpty()) {
            return str;
        }
    }
    return fallback;
}

int readIntOrDefault(const QJsonObject& obj, const char* key, int fallback, int minimum) {
    const auto value = obj.value(QLatin1String(key));
    if (value.isDouble()) {
        const int parsed = static_cast<int>(value.toInt());
        return parsed >= minimum ? parsed : fallback;
    }
    return fallback;
}
}  // namespace

AppConfig AppConfig::FromDefaults() {
    AppConfig config;
    return config;
}

AppConfig AppConfig::FromFile(const QString& path) {
    AppConfig config = FromDefaults();

    QFile file(path);
    if (!file.exists()) {
        config.source_ = QStringLiteral("defaults: missing %1").arg(path);
        return config;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        config.source_ = QStringLiteral("defaults: open failed (%1)").arg(file.errorString());
        return config;
    }

    const QByteArray data = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        config.source_ = QStringLiteral("defaults: parse error (%1)").arg(parseError.errorString());
        return config;
    }

    const QJsonObject obj = doc.object();

    config.serverUrl_ = readStringOrDefault(obj, "server_url", config.serverUrl_);
    config.restApiUrl_ = readStringOrDefault(obj, "rest_api_url", config.restApiUrl_);
    config.streamControlUrl_ = readStringOrDefault(obj, "stream_control_url", config.streamControlUrl_);
    config.licenseToken_ = readStringOrDefault(obj, "license_token", config.licenseToken_);
    config.licenseValidationUrl_ = readStringOrDefault(obj, "license_validation_url", config.licenseValidationUrl_);

    if (obj.contains(QLatin1String("client")) && obj.value(QLatin1String("client")).isObject()) {
        const QJsonObject clientObj = obj.value(QLatin1String("client")).toObject();
    config.reconnectIntervalMs_ =
            readIntOrDefault(clientObj, "reconnect_interval_ms", config.reconnectIntervalMs_, 500);
    }

    if (obj.contains(QLatin1String("server")) && obj.value(QLatin1String("server")).isObject()) {
        const QJsonObject serverObj = obj.value(QLatin1String("server")).toObject();
        config.keepAliveIntervalMs_ =
            readIntOrDefault(serverObj, "keepalive_interval_ms", config.keepAliveIntervalMs_, 500);
        config.listenPort_ = static_cast<quint16>(
            readIntOrDefault(serverObj, "listen_port", config.listenPort_, 1));
    }

    if (obj.contains(QLatin1String("rest_api")) && obj.value(QLatin1String("rest_api")).isObject()) {
        const QJsonObject restObj = obj.value(QLatin1String("rest_api")).toObject();
        config.restListenPort_ =
            static_cast<quint16>(readIntOrDefault(restObj, "listen_port", config.restListenPort_, 1));
    }

    if (obj.contains(QLatin1String("telegram")) && obj.value(QLatin1String("telegram")).isObject()) {
        const QJsonObject telegramObj = obj.value(QLatin1String("telegram")).toObject();
        config.telegramBotToken_ = readStringOrDefault(telegramObj, "bot_token", QString());
        config.telegramEnabled_ = telegramObj.value(QLatin1String("enabled")).toBool(false);
    }

    config.source_ = path;
    return config;
}

const QString& AppConfig::serverUrl() const noexcept {
    return serverUrl_;
}

int AppConfig::reconnectIntervalMs() const noexcept {
    return reconnectIntervalMs_;
}

int AppConfig::keepAliveIntervalMs() const noexcept {
    return keepAliveIntervalMs_;
}

quint16 AppConfig::listenPort() const noexcept {
    return listenPort_;
}

const QString& AppConfig::source() const noexcept {
    return source_;
}

const QString& AppConfig::streamControlUrl() const noexcept {
    return streamControlUrl_;
}

const QString& AppConfig::restApiUrl() const noexcept {
    return restApiUrl_;
}

quint16 AppConfig::restListenPort() const noexcept {
    return restListenPort_;
}

const QString& AppConfig::licenseToken() const noexcept {
    return licenseToken_;
}

const QString& AppConfig::licenseValidationUrl() const noexcept {
    return licenseValidationUrl_;
}

const QString& AppConfig::telegramBotToken() const noexcept {
    return telegramBotToken_;
}

bool AppConfig::telegramEnabled() const noexcept {
    return telegramEnabled_;
}

QUrl AppConfig::websocketUrl(const QString& endpoint) const {
    QUrl base(serverUrl_);
    if (!base.isValid()) {
        base = QUrl(QStringLiteral("http://localhost:8000"));
    }

    if (base.scheme().isEmpty()) {
        base.setScheme(QStringLiteral("http"));
    }

    if (base.scheme() == QStringLiteral("http")) {
        base.setScheme(QStringLiteral("ws"));
    } else if (base.scheme() == QStringLiteral("https")) {
        base.setScheme(QStringLiteral("wss"));
    }

    const QString normalizedEndpoint =
        endpoint.startsWith(QLatin1Char('/')) ? endpoint : QStringLiteral("/") + endpoint;
    return base.resolved(QUrl(normalizedEndpoint));
}

}  // namespace core
