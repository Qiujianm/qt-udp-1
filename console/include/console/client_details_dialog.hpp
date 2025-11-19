#pragma once

#include <QDialog>
#include <QMap>
#include <QNetworkReply>
#include <QPointer>
#include <QSqlDatabase>

class QLabel;
class QTableWidget;
class QTableWidgetItem;
class QTabWidget;
class QPushButton;
class QNetworkAccessManager;
class QListWidget;
class QLineEdit;

namespace console {

class MainWindow;

class ClientDetailsDialog final : public QDialog {
    Q_OBJECT
public:
    ClientDetailsDialog(const QString& clientId,
                        const QString& displayName,
                        QSqlDatabase db,
                        MainWindow* mainWindow,
                        QWidget* parent = nullptr);
    ~ClientDetailsDialog() override;

    QString clientId() const { return clientId_; }
    void refreshAlertsAndScreenshots();
    void refreshAllData();  // 刷新所有数据（应用统计、活动日志、截图、报警）
    void refreshActivities();  // 刷新活动日志（用于自动刷新）

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void handleReplyFinished(QNetworkReply* reply);
    void handleReload();
    void handleScreenshotSelectionChanged();
    void handleActivityDoubleClicked(QTableWidgetItem* item);
    void handleAlertDoubleClicked(QTableWidgetItem* item);
    void handleScreenshotOpen();
    void handleScreenshotSave();
    void handleScreenshotDelete();
    void handleSensitiveWordAdd();
    void handleSensitiveWordRemove();
    void handleSensitiveWordSync();
    void handleWorkHoursSave();
    void handlePauseClient();
    void handleResumeClient();
    void handleUploadNow();
    void handleUnlockScreen();
    void handleWindowScreenshotConfigButtonClicked();  // 打开配置对话框
    void handleWindowScreenshotAppAdd();
    void handleWindowScreenshotAppRemove();
    void handleWindowScreenshotAppSave();
    void loadWindowScreenshotApps();
    void createWindowScreenshotConfigDialog();  // 创建配置对话框

private:
    enum class RequestKind {
        AppUsage,
        Activities,
        Screenshots,
        ScreenshotPreview,
        ScreenshotDelete,
        AppUsageGlobal,
        Alerts,
        SensitiveWordsLoad,
        SensitiveWordsSave,
        TelegramChatIdLoad,
        TelegramChatIdSave,
        WorkHoursSave,
        ClientControl,
        WindowScreenshotAppsLoad,
        WindowScreenshotAppsSave
    };

    struct RequestInfo {
        RequestKind kind;
        QString payload;
    };

    void loadAppUsage();
    void loadActivities();
    void loadScreenshots();
    void loadGlobalAppStats();
    void loadGlobalAlerts();
    void loadSensitiveWords();
    void loadTelegramChatId();
    void handleTelegramChatIdSave();
    void requestScreenshotPreview(const QString& filename);
    void requestScreenshotDelete(const QString& filename);
    void populateAppUsage(const QJsonArray& apps);
    void populateActivities(const QJsonArray& activities);
    void populateScreenshots(const QJsonArray& screenshots);
    void populateGlobalAppStats(const QJsonArray& apps);
    void populateGlobalAlerts(const QJsonArray& alerts);
    void setStatus(QLabel* label, const QString& text, bool isError = false);
    void addRequest(QNetworkReply* reply, RequestKind kind, const QString& payload = QString());
    void sendClientCommand(const QJsonObject& payload, RequestKind kind);
    void updateScreenshotPreview(const QByteArray& bytes, const QString& filename);
    void resetScreenshotPreview();
    void focusScreenshotByTimestamp(const QString& timestamp);
    void focusScreenshotByFilename(const QString& filename);

    QString clientId_;
    QString displayName_;
    QSqlDatabase db_;  // 数据库连接
    MainWindow* mainWindow_{nullptr};  // 完全直连模式：从MainWindow获取数据
    QTabWidget* tabs_{nullptr};

    // 应用统计
    QWidget* appUsagePage_{nullptr};
    QLabel* appUsageStatus_{nullptr};
    QTableWidget* appUsageTable_{nullptr};
    QPushButton* appUsageRefresh_{nullptr};

    // 活动日志
    QWidget* activityPage_{nullptr};
    QLabel* activityStatus_{nullptr};
    QTableWidget* activityTable_{nullptr};
    QPushButton* activityRefresh_{nullptr};

    // 截图
    QWidget* screenshotPage_{nullptr};
    QLabel* screenshotStatus_{nullptr};
    QTableWidget* screenshotTable_{nullptr};
    QPushButton* screenshotRefresh_{nullptr};
    QLabel* screenshotPreview_{nullptr};
    QPushButton* screenshotOpen_{nullptr};
    QPushButton* screenshotSave_{nullptr};
    QPushButton* screenshotDelete_{nullptr};

    // 全局统计
    QWidget* globalAppPage_{nullptr};
    QLabel* globalAppStatus_{nullptr};
    QTableWidget* globalAppTable_{nullptr};
    QPushButton* globalAppRefresh_{nullptr};

    QWidget* alertPage_{nullptr};
    QLabel* alertStatus_{nullptr};
    QTableWidget* alertTable_{nullptr};
    QPushButton* alertRefresh_{nullptr};

    // 敏感词管理
    QWidget* sensitiveWordsPage_{nullptr};
    QLabel* sensitiveWordsStatus_{nullptr};
    QListWidget* sensitiveWordsList_{nullptr};
    QLineEdit* sensitiveWordEntry_{nullptr};
    QPushButton* sensitiveWordAdd_{nullptr};
    QPushButton* sensitiveWordRemove_{nullptr};
    QPushButton* sensitiveWordSync_{nullptr};
    
    // Telegram Chat ID
    QLineEdit* telegramChatIdEntry_{nullptr};
    QPushButton* telegramChatIdSave_{nullptr};
    
    // 工作时间段和远程控制
    QWidget* controlPage_{nullptr};
    QLineEdit* workStartTime_{nullptr};
    QLineEdit* workEndTime_{nullptr};
    QPushButton* workHoursSave_{nullptr};
    QPushButton* pauseButton_{nullptr};
    QPushButton* resumeButton_{nullptr};
    QPushButton* uploadNowButton_{nullptr};
    QPushButton* unlockScreenButton_{nullptr};
    
    // 窗口变更截图应用配置
    QPushButton* windowScreenshotConfigButton_{nullptr};  // 活动日志页面的配置按钮
    QDialog* windowScreenshotConfigDialog_{nullptr};  // 配置对话框
    QListWidget* windowScreenshotAppsList_{nullptr};
    QLineEdit* windowScreenshotAppEntry_{nullptr};
    QPushButton* windowScreenshotAppAdd_{nullptr};
    QPushButton* windowScreenshotAppRemove_{nullptr};
    QPushButton* windowScreenshotAppSave_{nullptr};

    QMap<QNetworkReply*, RequestInfo> pendingRequests_;
    bool initialLoadDone_{false};
    QString currentScreenshotFilename_;
    QByteArray currentScreenshotBytes_;
    bool screenshotPreviewLoading_{false};
    QTimer* autoRefreshTimer_{nullptr};  // 自动刷新定时器
    void setupAutoRefresh();  // 设置自动刷新
    void adjustColumnWidths();  // 调整列宽（特别是时间列）
};

}  // namespace console

