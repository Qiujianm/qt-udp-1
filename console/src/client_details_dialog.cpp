#include "console/client_details_dialog.hpp"
#include "console/main_window.hpp"

#include <QAbstractItemView>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QDir>
#include <QListWidget>
#include <QLineEdit>
#include <QGroupBox>
#include <QTime>
#include <QInputDialog>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <cstdlib>
#include <limits>

namespace console {

namespace {
constexpr int kColumnStretch = 1;

QString formatDuration(qint64 seconds) {
    if (seconds < 60) {
        return QObject::tr("%1 秒").arg(seconds);
    }
    const qint64 minutes = seconds / 60;
    if (minutes < 60) {
        return QObject::tr("%1 分钟").arg(minutes);
    }
    const qint64 hours = minutes / 60;
    const qint64 remainMinutes = minutes % 60;
    if (remainMinutes == 0) {
        return QObject::tr("%1 小时").arg(hours);
    }
    return QObject::tr("%1 小时 %2 分钟").arg(hours).arg(remainMinutes);
}

QString normalizeBaseUrl(const QString& base) {
    QString trimmed = base.trimmed();
    if (trimmed.isEmpty()) {
        // Try to get from environment or use CommandController default port
        const QString envUrl = qEnvironmentVariable("MONITOR_REST_API_URL");
        if (!envUrl.isEmpty()) {
            trimmed = envUrl;
        } else {
            return QStringLiteral("http://127.0.0.1:8080");
        }
    }
    QUrl url(trimmed);
    if (!url.isValid() || url.scheme().isEmpty()) {
        url = QUrl(QStringLiteral("http://%1").arg(trimmed));
    }
    if (!url.isValid()) {
        return QStringLiteral("http://127.0.0.1:8080");
    }
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("ws")) {
        url.setScheme(QStringLiteral("http"));
    } else if (scheme == QStringLiteral("wss")) {
        url.setScheme(QStringLiteral("https"));
    } else if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        url.setScheme(QStringLiteral("http"));
    }
    url.setFragment(QString());
    url.setQuery(QString());
    QString normalized = url.toString(QUrl::StripTrailingSlash);
    if (normalized.isEmpty()) {
        normalized = QStringLiteral("http://127.0.0.1:8080");
    }
    return normalized;
}

QUrl joinUrl(const QString& base, const QString& path) {
    QUrl url(base);
    QString sanitizedPath = path;
    if (!sanitizedPath.startsWith(QLatin1Char('/'))) {
        sanitizedPath.prepend(QLatin1Char('/'));
    }
    return url.resolved(QUrl(sanitizedPath));
}

}  // namespace

ClientDetailsDialog::ClientDetailsDialog(const QString& clientId,
                                         const QString& displayName,
                                         QSqlDatabase db,
                                         MainWindow* mainWindow,
                                         QWidget* parent)
    : QDialog(parent),
      clientId_(clientId),
      displayName_(displayName),
      db_(db),
      mainWindow_(mainWindow) {
    setWindowTitle(tr("客户端详情 - %1").arg(displayName_));
    resize(820, 620);

    auto* mainLayout = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);
    mainLayout->addWidget(tabs_);

    auto createStatusLabel = [](const QString& text) {
        auto* label = new QLabel(text);
        label->setObjectName(QStringLiteral("status"));
        label->setStyleSheet(QStringLiteral("color: #9ca3af;"));
        return label;
    };

    auto createTable = []() {
        auto* table = new QTableWidget();
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);
        table->setAlternatingRowColors(false);  // 统一背景颜色，不使用交替行颜色
        table->setStyleSheet(QStringLiteral(
            "QTableWidget { background-color: #000000; color: #e2e8f0; gridline-color: #1e293b; }"
            "QHeaderView::section { background-color: #1e3a8a; color: white; padding: 4px; }"
            "QTableWidget::item { background-color: #000000; color: #e2e8f0; }"
            "QTableWidget::item:selected { background-color: #3b82f6; color: white; }"));
        return table;
    };

    auto createPage = [&](const QString& title, QWidget*& page, QLabel*& statusLabel,
                          QTableWidget*& tableWidget, QPushButton*& refreshButton,
                          const QStringList& headers) {
        page = new QWidget(this);
        auto* vLayout = new QVBoxLayout(page);
        statusLabel = createStatusLabel(tr("正在加载…"));
        vLayout->addWidget(statusLabel);
        tableWidget = createTable();
        tableWidget->setColumnCount(headers.size());
        for (int i = 0; i < headers.size(); ++i) {
            tableWidget->setHorizontalHeaderItem(i, new QTableWidgetItem(headers.at(i)));
        }
        vLayout->addWidget(tableWidget, kColumnStretch);
        refreshButton = new QPushButton(tr("刷新"));
        refreshButton->setFixedWidth(96);
        refreshButton->setCursor(Qt::PointingHandCursor);
        vLayout->addWidget(refreshButton, 0, Qt::AlignRight);
        tabs_->addTab(page, title);
    };

    createPage(tr("应用统计"), appUsagePage_, appUsageStatus_, appUsageTable_, appUsageRefresh_,
               {tr("软件名称"), tr("使用时长"), tr("类别"), tr("最后使用")});
    createPage(tr("活动日志"), activityPage_, activityStatus_, activityTable_, activityRefresh_,
               {tr("时间"), tr("类型"), tr("详情")});
    
    // 在活动日志页面添加窗口变更截图配置按钮
    auto* activityButtonLayout = new QHBoxLayout();
    activityButtonLayout->addStretch();
    activityButtonLayout->addWidget(activityRefresh_);
    windowScreenshotConfigButton_ = new QPushButton(tr("⚙️ 窗口变更截图配置"));
    windowScreenshotConfigButton_->setCursor(Qt::PointingHandCursor);
    activityButtonLayout->addWidget(windowScreenshotConfigButton_);
    // 替换原来的刷新按钮布局
    static_cast<QVBoxLayout*>(activityPage_->layout())->removeWidget(activityRefresh_);
    static_cast<QVBoxLayout*>(activityPage_->layout())->addLayout(activityButtonLayout);
    
    createPage(tr("截图"), screenshotPage_, screenshotStatus_, screenshotTable_, screenshotRefresh_,
               {tr("时间"), tr("文件"), tr("类别"), tr("大小")});
    // 应用排行榜功能已移除
    createPage(tr("敏感词预警"), alertPage_, alertStatus_, alertTable_, alertRefresh_,
               {tr("时间"), tr("关键词"), tr("窗口/应用"), tr("类型"), tr("上下文")});

    // 敏感词管理页面
    sensitiveWordsPage_ = new QWidget(this);
    auto* sensitiveWordsLayout = new QVBoxLayout(sensitiveWordsPage_);
    sensitiveWordsStatus_ = createStatusLabel(tr("正在加载…"));
    sensitiveWordsLayout->addWidget(sensitiveWordsStatus_);
    
    sensitiveWordsList_ = new QListWidget();
    sensitiveWordsList_->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: #0f172a; color: #e2e8f0; border: 1px solid #1e293b; }"
        "QListWidget::item:selected { background-color: #3b82f6; color: white; }"
        "QListWidget::item:hover { background-color: #1e293b; }"));
    sensitiveWordsLayout->addWidget(sensitiveWordsList_, kColumnStretch);
    
    auto* sensitiveWordsControlLayout = new QHBoxLayout();
    sensitiveWordEntry_ = new QLineEdit();
    sensitiveWordEntry_->setPlaceholderText(tr("输入敏感词"));
    sensitiveWordEntry_->setStyleSheet(QStringLiteral(
        "QLineEdit { background-color: #1e293b; color: #e2e8f0; border: 1px solid #334155; padding: 4px; }"));
    sensitiveWordsControlLayout->addWidget(sensitiveWordEntry_);
    
    sensitiveWordAdd_ = new QPushButton(tr("➕ 添加"));
    sensitiveWordAdd_->setCursor(Qt::PointingHandCursor);
    sensitiveWordsControlLayout->addWidget(sensitiveWordAdd_);
    
    sensitiveWordRemove_ = new QPushButton(tr("➖ 删除选中"));
    sensitiveWordRemove_->setCursor(Qt::PointingHandCursor);
    sensitiveWordsControlLayout->addWidget(sensitiveWordRemove_);
    
    sensitiveWordSync_ = new QPushButton(tr("💾 保存并同步"));
    sensitiveWordSync_->setCursor(Qt::PointingHandCursor);
    sensitiveWordsControlLayout->addWidget(sensitiveWordSync_);
    
    sensitiveWordsLayout->addLayout(sensitiveWordsControlLayout);
    
    // Telegram Chat ID 设置
    auto* telegramLayout = new QHBoxLayout();
    auto* telegramLabel = new QLabel(tr("Telegram Chat ID:"));
    telegramLabel->setStyleSheet(QStringLiteral("color: #000000;"));
    telegramLayout->addWidget(telegramLabel);
    
    telegramChatIdEntry_ = new QLineEdit();
    telegramChatIdEntry_->setPlaceholderText(tr("输入 Telegram Chat ID (例如: 123456789)"));
    telegramChatIdEntry_->setStyleSheet(QStringLiteral(
        "QLineEdit { background-color: #1e293b; color: #e2e8f0; border: 1px solid #334155; padding: 4px; }"));
    telegramLayout->addWidget(telegramChatIdEntry_);
    
    telegramChatIdSave_ = new QPushButton(tr("💾 保存"));
    telegramChatIdSave_->setCursor(Qt::PointingHandCursor);
    telegramLayout->addWidget(telegramChatIdSave_);
    
    sensitiveWordsLayout->addLayout(telegramLayout);
    tabs_->addTab(sensitiveWordsPage_, tr("敏感词设置"));
    
    // 控制页面
    controlPage_ = new QWidget();
    auto* controlLayout = new QVBoxLayout(controlPage_);
    controlLayout->setSpacing(12);
    
    // 工作时间段设置
    auto* workHoursGroup = new QGroupBox(tr("工作时间段设置"));
    workHoursGroup->setStyleSheet(QStringLiteral(
        "QGroupBox { color: #e2e8f0; border: 1px solid #334155; padding-top: 10px; margin-top: 10px; }"));
    auto* workHoursLayout = new QHBoxLayout();
    
    auto* startLabel = new QLabel(tr("开始时间:"));
    startLabel->setStyleSheet(QStringLiteral("color: #e2e8f0;"));
    workStartTime_ = new QLineEdit();
    workStartTime_->setPlaceholderText(tr("HH:mm (例如: 09:00)"));
    workStartTime_->setStyleSheet(QStringLiteral(
        "QLineEdit { background-color: #1e293b; color: #e2e8f0; border: 1px solid #334155; padding: 4px; }"));
    
    auto* endLabel = new QLabel(tr("结束时间:"));
    endLabel->setStyleSheet(QStringLiteral("color: #e2e8f0;"));
    workEndTime_ = new QLineEdit();
    workEndTime_->setPlaceholderText(tr("HH:mm (例如: 18:00)"));
    workEndTime_->setStyleSheet(QStringLiteral(
        "QLineEdit { background-color: #1e293b; color: #e2e8f0; border: 1px solid #334155; padding: 4px; }"));
    
    workHoursSave_ = new QPushButton(tr("💾 保存工作时间"));
    workHoursSave_->setCursor(Qt::PointingHandCursor);
    
    workHoursLayout->addWidget(startLabel);
    workHoursLayout->addWidget(workStartTime_);
    workHoursLayout->addWidget(endLabel);
    workHoursLayout->addWidget(workEndTime_);
    workHoursLayout->addWidget(workHoursSave_);
    workHoursLayout->addStretch();
    workHoursGroup->setLayout(workHoursLayout);
    controlLayout->addWidget(workHoursGroup);
    
    // 远程控制按钮
    auto* controlGroup = new QGroupBox(tr("远程控制"));
    controlGroup->setStyleSheet(QStringLiteral(
        "QGroupBox { color: #e2e8f0; border: 1px solid #334155; padding-top: 10px; margin-top: 10px; }"));
    auto* controlButtonLayout = new QHBoxLayout();
    
    pauseButton_ = new QPushButton(tr("⏸ 暂停所有功能"));
    pauseButton_->setCursor(Qt::PointingHandCursor);
    resumeButton_ = new QPushButton(tr("▶ 恢复所有功能"));
    resumeButton_->setCursor(Qt::PointingHandCursor);
    uploadNowButton_ = new QPushButton(tr("📤 立即上传数据"));
    uploadNowButton_->setCursor(Qt::PointingHandCursor);
    unlockScreenButton_ = new QPushButton(tr("🔓 解锁锁屏"));
    unlockScreenButton_->setCursor(Qt::PointingHandCursor);
    
    controlButtonLayout->addWidget(pauseButton_);
    controlButtonLayout->addWidget(resumeButton_);
    controlButtonLayout->addWidget(uploadNowButton_);
    controlButtonLayout->addWidget(unlockScreenButton_);
    controlButtonLayout->addStretch();
    controlGroup->setLayout(controlButtonLayout);
    controlLayout->addWidget(controlGroup);
    
    controlLayout->addStretch();
    tabs_->addTab(controlPage_, tr("远程控制"));
    
    connect(workHoursSave_, &QPushButton::clicked, this, &ClientDetailsDialog::handleWorkHoursSave);
    connect(pauseButton_, &QPushButton::clicked, this, &ClientDetailsDialog::handlePauseClient);
    connect(resumeButton_, &QPushButton::clicked, this, &ClientDetailsDialog::handleResumeClient);
    connect(uploadNowButton_, &QPushButton::clicked, this, &ClientDetailsDialog::handleUploadNow);
    connect(unlockScreenButton_, &QPushButton::clicked, this, &ClientDetailsDialog::handleUnlockScreen);

    // 移除 QNetworkAccessManager finished 信号连接（纯数据库模式）
    connect(appUsageRefresh_, &QPushButton::clicked, this, &ClientDetailsDialog::handleReload);
    connect(activityRefresh_, &QPushButton::clicked, this, &ClientDetailsDialog::handleReload);
    connect(screenshotRefresh_, &QPushButton::clicked, this, &ClientDetailsDialog::handleReload);
    connect(alertRefresh_, &QPushButton::clicked, this, &ClientDetailsDialog::handleReload);
    connect(sensitiveWordAdd_, &QPushButton::clicked, this, &ClientDetailsDialog::handleSensitiveWordAdd);
    connect(sensitiveWordRemove_, &QPushButton::clicked, this, &ClientDetailsDialog::handleSensitiveWordRemove);
    connect(sensitiveWordSync_, &QPushButton::clicked, this, &ClientDetailsDialog::handleSensitiveWordSync);
    connect(sensitiveWordEntry_, &QLineEdit::returnPressed, this, &ClientDetailsDialog::handleSensitiveWordAdd);
    connect(telegramChatIdSave_, &QPushButton::clicked, this, &ClientDetailsDialog::handleTelegramChatIdSave);
    connect(workHoursSave_, &QPushButton::clicked, this, &ClientDetailsDialog::handleWorkHoursSave);
    connect(pauseButton_, &QPushButton::clicked, this, &ClientDetailsDialog::handlePauseClient);
    connect(resumeButton_, &QPushButton::clicked, this, &ClientDetailsDialog::handleResumeClient);
    connect(uploadNowButton_, &QPushButton::clicked, this, &ClientDetailsDialog::handleUploadNow);
    connect(unlockScreenButton_, &QPushButton::clicked, this, &ClientDetailsDialog::handleUnlockScreen);
    connect(windowScreenshotConfigButton_, &QPushButton::clicked, this, &ClientDetailsDialog::handleWindowScreenshotConfigButtonClicked);

    screenshotPreview_ = new QLabel(screenshotPage_);
    screenshotPreview_->setAlignment(Qt::AlignCenter);
    screenshotPreview_->setMinimumHeight(220);
    screenshotPreview_->setStyleSheet(QStringLiteral(
        "background-color: rgba(15,23,42,0.6);"
        "border: 1px dashed rgba(148,163,184,0.4);"
        "color: #64748b;"));
    static_cast<QVBoxLayout*>(screenshotPage_->layout())->insertWidget(2, screenshotPreview_);

    auto* buttonBar = new QHBoxLayout();
    screenshotOpen_ = new QPushButton(tr("打开"));
    screenshotSave_ = new QPushButton(tr("保存"));
    screenshotDelete_ = new QPushButton(tr("删除"));
    for (QPushButton* btn : {screenshotOpen_, screenshotSave_, screenshotDelete_}) {
        btn->setCursor(Qt::PointingHandCursor);
        btn->setEnabled(false);
    }
    buttonBar->addStretch();
    buttonBar->addWidget(screenshotOpen_);
    buttonBar->addWidget(screenshotSave_);
    buttonBar->addWidget(screenshotDelete_);
    buttonBar->addStretch();
    static_cast<QVBoxLayout*>(screenshotPage_->layout())->insertLayout(3, buttonBar);

    connect(screenshotTable_, &QTableWidget::itemSelectionChanged, this,
            &ClientDetailsDialog::handleScreenshotSelectionChanged);
    connect(screenshotTable_, &QTableWidget::itemDoubleClicked, this,
            &ClientDetailsDialog::handleScreenshotOpen);
    connect(screenshotOpen_, &QPushButton::clicked, this, &ClientDetailsDialog::handleScreenshotOpen);
    connect(screenshotSave_, &QPushButton::clicked, this, &ClientDetailsDialog::handleScreenshotSave);
    connect(screenshotDelete_, &QPushButton::clicked, this, &ClientDetailsDialog::handleScreenshotDelete);
    connect(activityTable_, &QTableWidget::itemDoubleClicked, this,
            &ClientDetailsDialog::handleActivityDoubleClicked);
    connect(alertTable_, &QTableWidget::itemDoubleClicked, this,
            &ClientDetailsDialog::handleAlertDoubleClicked);
    
    // 设置自动刷新和列宽
    setupAutoRefresh();
    adjustColumnWidths();
}

ClientDetailsDialog::~ClientDetailsDialog() = default;

void ClientDetailsDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    // 每次显示对话框时都重新加载数据，确保显示最新内容
    QTimer::singleShot(0, this, [this]() {
        loadAppUsage();
        loadActivities();
        loadScreenshots();
        loadGlobalAlerts();
        loadSensitiveWords();
        loadTelegramChatId();
        loadWindowScreenshotApps();
    });
    // 只在第一次加载时设置标志，用于其他初始化逻辑
    if (!initialLoadDone_) {
        initialLoadDone_ = true;
    }
}

void ClientDetailsDialog::handleReload() {
    QObject* src = sender();
    if (src == appUsageRefresh_) {
        loadAppUsage();
    } else if (src == activityRefresh_) {
        loadActivities();
    } else if (src == screenshotRefresh_) {
        loadScreenshots();
    } else if (src == alertRefresh_) {
        loadGlobalAlerts();
    }
    // Note: Sensitive words don't have a refresh button, they reload automatically on tab show
}

void ClientDetailsDialog::refreshAlertsAndScreenshots() {
    // 刷新报警列表和截图列表（用于实时更新）
    loadGlobalAlerts();
    loadScreenshots();
}

void ClientDetailsDialog::refreshAllData() {
    // 刷新所有数据（应用统计、活动日志、截图、报警）
    loadAppUsage();
    loadActivities();
    loadScreenshots();
    loadGlobalAlerts();
}

void ClientDetailsDialog::refreshActivities() {
    loadActivities();
}

void ClientDetailsDialog::addRequest(QNetworkReply* reply, RequestKind kind, const QString& payload) {
    if (!reply) {
        return;
    }
    pendingRequests_.insert(reply, RequestInfo{kind, payload});
    reply->setProperty("requestKind", static_cast<int>(kind));
}

void ClientDetailsDialog::handleReplyFinished(QNetworkReply* reply) {
    if (!reply) {
        return;
    }
    const auto info = pendingRequests_.take(reply);
    
    // Check HTTP status code
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError || statusCode >= 400) {
        QString err;
        if (statusCode >= 400) {
            err = tr("HTTP %1").arg(statusCode);
            if (statusCode == 404) {
                err = tr("资源未找到 (404)");
            } else if (statusCode == 500) {
                err = tr("服务器错误 (500)");
            } else if (statusCode == 503) {
                err = tr("服务不可用 (503)");
            }
        } else {
            err = reply->errorString();
        }
        
        switch (info.kind) {
        case RequestKind::AppUsage:
            setStatus(appUsageStatus_, tr("加载失败：%1").arg(err), true);
            break;
        case RequestKind::Activities:
            setStatus(activityStatus_, tr("加载失败：%1").arg(err), true);
            break;
        case RequestKind::Screenshots:
            setStatus(screenshotStatus_, tr("加载失败：%1").arg(err), true);
            break;
        case RequestKind::ScreenshotPreview:
            screenshotPreviewLoading_ = false;
            setStatus(screenshotStatus_, tr("加载失败：%1").arg(err), true);
            break;
        case RequestKind::ScreenshotDelete:
            setStatus(screenshotStatus_, tr("删除失败：%1").arg(err), true);
            break;
        case RequestKind::AppUsageGlobal:
            setStatus(globalAppStatus_, tr("加载失败：%1").arg(err), true);
            break;
        case RequestKind::Alerts:
            setStatus(alertStatus_, tr("加载失败：%1").arg(err), true);
            break;
        case RequestKind::SensitiveWordsLoad:
            setStatus(sensitiveWordsStatus_, tr("加载失败：%1").arg(err), true);
            break;
        case RequestKind::SensitiveWordsSave:
            setStatus(sensitiveWordsStatus_, tr("保存失败：%1").arg(err), true);
            break;
        case RequestKind::TelegramChatIdLoad:
            // No status label for Telegram Chat ID, ignore error
            break;
        case RequestKind::TelegramChatIdSave:
            QMessageBox::warning(this, tr("错误"), tr("保存 Telegram Chat ID 失败：%1").arg(err));
            break;
        case RequestKind::WindowScreenshotAppsLoad:
            // No status label for window screenshot apps, ignore error
            break;
        case RequestKind::WindowScreenshotAppsSave:
            QMessageBox::warning(this, tr("错误"), tr("保存窗口变更截图应用配置失败：%1").arg(err));
            break;
        }
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    const QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();
    switch (info.kind) {
    case RequestKind::AppUsage:
        if (doc.isArray()) {
            populateAppUsage(doc.array());
        } else {
            populateAppUsage(obj.value(QStringLiteral("apps")).toArray());
        }
        break;
    case RequestKind::AppUsageGlobal:
        // 应用排行榜功能已移除
        break;
    case RequestKind::Activities:
        if (doc.isArray()) {
            populateActivities(doc.array());
        } else {
            populateActivities(obj.value(QStringLiteral("activities")).toArray());
        }
        break;
    case RequestKind::Screenshots:
        if (doc.isArray()) {
            populateScreenshots(doc.array());
        } else {
            populateScreenshots(obj.value(QStringLiteral("screenshots")).toArray());
        }
        break;
    case RequestKind::ScreenshotPreview:
        updateScreenshotPreview(payload, info.payload);
        break;
    case RequestKind::ScreenshotDelete:
        setStatus(screenshotStatus_, tr("删除成功"));
        loadScreenshots();
        break;
    case RequestKind::Alerts:
        // 降级为 DEBUG：预警数据解析是正常操作，不需要 INFO 级别日志
        qDebug() << "[Console] Received alerts response, isArray=" << doc.isArray() 
                << "payload size=" << payload.size();
        if (doc.isArray()) {
            qDebug() << "[Console] Parsing alerts as array, count=" << doc.array().size();
            populateGlobalAlerts(doc.array());
        } else {
            QJsonArray alertsArray = obj.value(QStringLiteral("alerts")).toArray();
            qDebug() << "[Console] Parsing alerts from object, count=" << alertsArray.size();
            if (alertsArray.isEmpty() && !obj.isEmpty()) {
                qWarning() << "[Console] Alerts array is empty but object is not. Object keys:" << obj.keys();
            }
            populateGlobalAlerts(alertsArray);
        }
        break;
    case RequestKind::SensitiveWordsLoad: {
        QJsonArray wordsArray;
        if (doc.isArray()) {
            wordsArray = doc.array();
        } else {
            wordsArray = obj.value(QStringLiteral("words")).toArray();
        }
        sensitiveWordsList_->clear();
        for (const QJsonValue& value : wordsArray) {
            if (value.isString()) {
                sensitiveWordsList_->addItem(value.toString());
            }
        }
        setStatus(sensitiveWordsStatus_, tr("共 %1 个敏感词").arg(wordsArray.size()));
        break;
    }
    case RequestKind::SensitiveWordsSave: {
        const QString status = obj.value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("ok")) {
            const int count = obj.value(QStringLiteral("count")).toInt();
            setStatus(sensitiveWordsStatus_, tr("已保存并同步 %1 个敏感词").arg(count));
            QMessageBox::information(this, tr("成功"), tr("已保存并同步 %1 个敏感词").arg(count));
        } else {
            setStatus(sensitiveWordsStatus_, tr("保存失败"), true);
            QMessageBox::warning(this, tr("失败"), tr("同步失败"));
        }
        break;
    }
    case RequestKind::TelegramChatIdLoad: {
        // Load chat_id from clients API response
        if (doc.isArray()) {
            for (const QJsonValue& value : doc.array()) {
                const QJsonObject clientObj = value.toObject();
                if (clientObj.value(QStringLiteral("client_id")).toString() == clientId_) {
                    const QString chatId = clientObj.value(QStringLiteral("telegram_chat_id")).toString();
                    telegramChatIdEntry_->setText(chatId);
                    break;
                }
            }
        } else if (obj.contains(QStringLiteral("telegram_chat_id"))) {
            telegramChatIdEntry_->setText(obj.value(QStringLiteral("telegram_chat_id")).toString());
        }
        break;
    }
    case RequestKind::TelegramChatIdSave: {
        const QString status = obj.value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("ok")) {
            QMessageBox::information(this, tr("成功"), tr("Telegram Chat ID 已保存"));
        } else {
            QMessageBox::warning(this, tr("失败"), tr("保存失败"));
        }
        break;
    }
    case RequestKind::WorkHoursSave: {
        const QString status = obj.value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("ok")) {
            QMessageBox::information(this, tr("成功"), tr("工作时间段已设置"));
        } else {
            QMessageBox::warning(this, tr("失败"), tr("设置失败"));
        }
        break;
    }
    case RequestKind::ClientControl: {
        const QString status = obj.value(QStringLiteral("status")).toString();
        if (status != QStringLiteral("ok")) {
            QMessageBox::warning(this, tr("失败"), tr("命令发送失败"));
        }
        break;
    }
    case RequestKind::WindowScreenshotAppsLoad: {
        QJsonArray appsArray = obj.value(QStringLiteral("apps")).toArray();
        if (windowScreenshotAppsList_) {
            windowScreenshotAppsList_->clear();
            for (const QJsonValue& value : appsArray) {
                if (value.isString()) {
                    windowScreenshotAppsList_->addItem(value.toString());
                }
            }
        }
        break;
    }
    case RequestKind::WindowScreenshotAppsSave: {
        const QString status = obj.value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("ok")) {
            const int count = obj.value(QStringLiteral("inserted")).toInt();
            QMessageBox::information(windowScreenshotConfigDialog_ ? windowScreenshotConfigDialog_ : this, 
                                    tr("成功"), tr("已保存 %1 个窗口变更截图应用配置").arg(count));
            // 重新加载配置以确认保存成功
            loadWindowScreenshotApps();
        } else {
            QMessageBox::warning(windowScreenshotConfigDialog_ ? windowScreenshotConfigDialog_ : this, 
                                tr("失败"), tr("保存失败"));
        }
        break;
    }
    }
}

void ClientDetailsDialog::loadAppUsage() {
    setStatus(appUsageStatus_, tr("正在加载…"));
    appUsageTable_->setRowCount(0);

    if (!db_.isValid()) {
        setStatus(appUsageStatus_, tr("数据库不可用"), true);
        return;
    }

    // 从数据库查询该客户端的应用使用记录
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "SELECT app_name, total_seconds, timestamp FROM app_usage "
        "WHERE client_id = :client_id "
        "ORDER BY timestamp DESC"));
    query.bindValue(QStringLiteral(":client_id"), clientId_);
    
    if (!query.exec()) {
        setStatus(appUsageStatus_, tr("查询失败: %1").arg(query.lastError().text()), true);
        return;
    }

    // 构建 JSON 数组传给 populateAppUsage（复用现有显示逻辑）
    QJsonArray apps;
    while (query.next()) {
        QJsonObject obj;
        obj[QStringLiteral("name")] = query.value(0).toString();
        obj[QStringLiteral("total_duration")] = query.value(1).toLongLong();
        obj[QStringLiteral("timestamp")] = query.value(2).toString();
        obj[QStringLiteral("category")] = tr("未分类");
        apps.append(obj);
    }

    populateAppUsage(apps);
    setStatus(appUsageStatus_, tr("已加载 %1 条记录").arg(apps.size()));
}

void ClientDetailsDialog::loadActivities() {
    setStatus(activityStatus_, tr("正在加载…"));
    activityTable_->setRowCount(0);
    
    // 纯UDP模式：从 MainWindow 内存获取数据（暂时未存储到数据库）
    if (mainWindow_) {
        const QJsonArray activities = mainWindow_->getClientActivities(clientId_);
        qDebug() << "[Console] loadActivities: loaded" << activities.size() << "activities from MainWindow";
        if (!activities.isEmpty()) {
            populateActivities(activities);
            setStatus(activityStatus_, tr("已加载 %1 条记录").arg(activities.size()));
            return;
        }
    }
    
    setStatus(activityStatus_, tr("暂无活动日志"));
}

void ClientDetailsDialog::loadScreenshots() {
    setStatus(screenshotStatus_, tr("正在加载…"));
    screenshotTable_->setRowCount(0);
    resetScreenshotPreview();
    
    // 纯UDP模式：从 MainWindow 内存获取数据
    if (mainWindow_) {
        const QMap<QString, QByteArray> screenshots = mainWindow_->getClientScreenshots(clientId_);
        qDebug() << "[Console] loadScreenshots: loaded" << screenshots.size() << "screenshots from MainWindow";
        if (!screenshots.isEmpty()) {
            // 转换为 QJsonArray 格式供 populateScreenshots 使用
            QJsonArray screenshotsArray;
            for (auto it = screenshots.begin(); it != screenshots.end(); ++it) {
                QJsonObject obj;
                QString timestamp = it.key();
                if (timestamp.length() > 19) {
                    timestamp = timestamp.left(19);
                }
                obj[QStringLiteral("timestamp")] = timestamp;
                QString filename = QStringLiteral("screenshot_%1.jpg").arg(
                    timestamp.replace(QStringLiteral(":"), QStringLiteral("_"))
                             .replace(QStringLiteral("-"), QStringLiteral("_")));
                obj[QStringLiteral("filename")] = filename;
                obj[QStringLiteral("size")] = static_cast<qint64>(it.value().size());
                obj[QStringLiteral("is_alert")] = true;
                screenshotsArray.append(obj);
            }
            populateScreenshots(screenshotsArray);
            setStatus(screenshotStatus_, tr("已加载 %1 条记录").arg(screenshots.size()));
            return;
        }
    }
    
    setStatus(screenshotStatus_, tr("暂无截图数据"));
}

// 应用排行榜功能已移除

void ClientDetailsDialog::loadGlobalAlerts() {
    setStatus(alertStatus_, tr("正在加载…"));
    if (alertTable_) {
        alertTable_->setRowCount(0);
    }

    if (!db_.isValid()) {
        setStatus(alertStatus_, tr("数据库不可用"), true);
        return;
    }

    // 从数据库查询该客户端的告警记录
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "SELECT alert_type, keyword, window_title, context, timestamp FROM alerts "
        "WHERE client_id = :client_id "
        "ORDER BY timestamp DESC"));
    query.bindValue(QStringLiteral(":client_id"), clientId_);
    
    if (!query.exec()) {
        setStatus(alertStatus_, tr("查询失败: %1").arg(query.lastError().text()), true);
        return;
    }

    // 构建 JSON 数组传给 populateGlobalAlerts
    QJsonArray alerts;
    while (query.next()) {
        QJsonObject obj;
        obj[QStringLiteral("alert_type")] = query.value(0).toString();
        obj[QStringLiteral("keyword")] = query.value(1).toString();
        obj[QStringLiteral("window_title")] = query.value(2).toString();
        obj[QStringLiteral("context")] = query.value(3).toString();
        obj[QStringLiteral("timestamp")] = query.value(4).toString();
        alerts.append(obj);
    }

    populateGlobalAlerts(alerts);
    setStatus(alertStatus_, tr("已加载 %1 条记录").arg(alerts.size()));
}

void ClientDetailsDialog::loadSensitiveWords() {
    setStatus(sensitiveWordsStatus_, tr("正在加载…"));
    sensitiveWordsList_->clear();
    
    // 纯UDP模式：从 MainWindow 获取敏感词列表
    if (mainWindow_) {
        const QStringList words = mainWindow_->loadSensitiveWords();
        for (const QString& word : words) {
            sensitiveWordsList_->addItem(word);
        }
        setStatus(sensitiveWordsStatus_, tr("已加载 %1 个敏感词").arg(words.size()));
    } else {
        setStatus(sensitiveWordsStatus_, tr("暂无敏感词"));
    }
}

void ClientDetailsDialog::loadTelegramChatId() {
    // 纯UDP模式：Telegram Chat ID 存储在数据库中
    if (!db_.isValid()) {
        return;
    }
    
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "SELECT telegram_chat_id FROM clients WHERE client_id = :client_id"));
    query.bindValue(QStringLiteral(":client_id"), clientId_);
    
    if (query.exec() && query.next()) {
        const QString chatId = query.value(0).toString();
        telegramChatIdEntry_->setText(chatId);
    }
}

void ClientDetailsDialog::handleTelegramChatIdSave() {
    const QString chatId = telegramChatIdEntry_->text().trimmed();
    
    // 纯UDP模式：直接更新数据库
    if (!db_.isValid()) {
        QMessageBox::warning(this, tr("错误"), tr("数据库未连接"));
        return;
    }
    
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "UPDATE clients SET telegram_chat_id = :chat_id WHERE client_id = :client_id"));
    query.bindValue(QStringLiteral(":chat_id"), chatId);
    query.bindValue(QStringLiteral(":client_id"), clientId_);
    
    if (query.exec()) {
        QMessageBox::information(this, tr("成功"), tr("Telegram Chat ID 已保存"));
    } else {
        QMessageBox::warning(this, tr("错误"), 
            tr("保存失败: %1").arg(query.lastError().text()));
    }
}

void ClientDetailsDialog::handleSensitiveWordAdd() {
    const QString word = sensitiveWordEntry_->text().trimmed();
    if (word.isEmpty()) {
        return;
    }
    // Check for duplicates
    for (int i = 0; i < sensitiveWordsList_->count(); ++i) {
        if (sensitiveWordsList_->item(i)->text().compare(word, Qt::CaseInsensitive) == 0) {
            QMessageBox::information(this, tr("提示"), tr("敏感词已存在"));
            return;
        }
    }
    sensitiveWordsList_->addItem(word);
    sensitiveWordEntry_->clear();
    setStatus(sensitiveWordsStatus_, tr("共 %1 个敏感词").arg(sensitiveWordsList_->count()));
}

void ClientDetailsDialog::handleSensitiveWordRemove() {
    const QList<QListWidgetItem*> selected = sensitiveWordsList_->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先选择要删除的敏感词"));
        return;
    }
    for (QListWidgetItem* item : selected) {
        delete sensitiveWordsList_->takeItem(sensitiveWordsList_->row(item));
    }
    setStatus(sensitiveWordsStatus_, tr("共 %1 个敏感词").arg(sensitiveWordsList_->count()));
}

void ClientDetailsDialog::handleSensitiveWordSync() {
    QStringList words;
    for (int i = 0; i < sensitiveWordsList_->count(); ++i) {
        const QString word = sensitiveWordsList_->item(i)->text().trimmed();
        if (!word.isEmpty()) {
            words.append(word);
        }
    }
    words.removeDuplicates();
    
    if (words.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("敏感词列表为空"));
        return;
    }
    
    setStatus(sensitiveWordsStatus_, tr("正在保存…"));
    
    // 纯UDP模式：保存到数据库
    if (!db_.isValid()) {
        QMessageBox::warning(this, tr("错误"), tr("数据库未连接"));
        setStatus(sensitiveWordsStatus_, tr("保存失败"));
        return;
    }
    
    // 清空现有敏感词
    QSqlQuery deleteQuery(db_);
    deleteQuery.exec(QStringLiteral("DELETE FROM sensitive_words"));
    
    // 插入新敏感词
    QSqlQuery insertQuery(db_);
    insertQuery.prepare(QStringLiteral(
        "INSERT INTO sensitive_words (word, created_at) VALUES (:word, :created_at)"));
    
    int successCount = 0;
    for (const QString& word : words) {
        insertQuery.bindValue(QStringLiteral(":word"), word);
        insertQuery.bindValue(QStringLiteral(":created_at"), 
            QDateTime::currentDateTime().toString(Qt::ISODate));
        if (insertQuery.exec()) {
            ++successCount;
        }
    }
    
    setStatus(sensitiveWordsStatus_, tr("已保存 %1 个敏感词").arg(successCount));
    QMessageBox::information(this, tr("成功"), 
        tr("敏感词已保存，重启客户端后生效"));
}

void ClientDetailsDialog::requestScreenshotPreview(const QString& filename) {
    if (filename.isEmpty() || screenshotPreviewLoading_) {
        return;
    }
    screenshotPreviewLoading_ = true;
    setStatus(screenshotStatus_, tr("正在加载截图…"));
    screenshotOpen_->setEnabled(false);
    screenshotSave_->setEnabled(false);
    
    // 纯UDP模式：从本地截图目录读取
    const QString screenshotPath = QStringLiteral("./screenshots/%1/%2").arg(clientId_, filename);
    QFile file(screenshotPath);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(screenshotStatus_, tr("加载失败"));
        screenshotPreviewLoading_ = false;
        QMessageBox::warning(this, tr("错误"), tr("无法读取截图文件: %1").arg(file.errorString()));
        return;
    }
    
    const QByteArray imageData = file.readAll();
    file.close();
    
    QImage image;
    if (!image.loadFromData(imageData)) {
        setStatus(screenshotStatus_, tr("加载失败"));
        screenshotPreviewLoading_ = false;
        QMessageBox::warning(this, tr("错误"), tr("无法解析截图图像"));
        return;
    }
    
    const QPixmap pixmap = QPixmap::fromImage(image);
    if (pixmap.isNull()) {
        setStatus(screenshotStatus_, tr("加载失败"));
        screenshotPreviewLoading_ = false;
        return;
    }
    
    screenshotPreview_->setPixmap(pixmap.scaled(
        screenshotPreview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    setStatus(screenshotStatus_, tr("已加载"));
    screenshotOpen_->setEnabled(true);
    screenshotSave_->setEnabled(true);
    screenshotPreviewLoading_ = false;
}

void ClientDetailsDialog::requestScreenshotDelete(const QString& filename) {
    if (filename.isEmpty()) {
        return;
    }
    setStatus(screenshotStatus_, tr("正在删除…"));
    
    // 纯UDP模式：删除本地截图文件
    const QString screenshotPath = QStringLiteral("./screenshots/%1/%2").arg(clientId_, filename);
    QFile file(screenshotPath);
    if (!file.remove()) {
        setStatus(screenshotStatus_, tr("删除失败"));
        QMessageBox::warning(this, tr("错误"), tr("无法删除截图文件: %1").arg(file.errorString()));
        return;
    }
    
    // 从数据库删除记录
    if (db_.isValid()) {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "DELETE FROM screenshots WHERE client_id = :client_id AND filename = :filename"));
        query.bindValue(QStringLiteral(":client_id"), clientId_);
        query.bindValue(QStringLiteral(":filename"), filename);
        query.exec();
    }
    
    setStatus(screenshotStatus_, tr("已删除"));
    loadScreenshots();  // 重新加载列表
}

void ClientDetailsDialog::populateAppUsage(const QJsonArray& apps) {
    appUsageTable_->setRowCount(0);
    if (apps.isEmpty()) {
        setStatus(appUsageStatus_, tr("暂无数据"));
        qDebug() << "[Console] populateAppUsage: apps array is empty";
        return;
    }

    qDebug() << "[Console] populateAppUsage: populating" << apps.size() << "apps";
    for (int i = 0; i < apps.size(); ++i) {
        const QJsonObject obj = apps.at(i).toObject();
        const QString name = obj.value(QStringLiteral("name")).toString(obj.value(QStringLiteral("software_name")).toString(tr("未知应用")));
        const qint64 durationSec = obj.value(QStringLiteral("total_duration")).toVariant().toLongLong();
        const QString category = obj.value(QStringLiteral("category")).toString(
            obj.value(QStringLiteral("type")).toString(tr("未分类")));
        const QString timestamp = obj.value(QStringLiteral("timestamp")).toString(
            obj.value(QStringLiteral("last_used")).toString());

        const int row = appUsageTable_->rowCount();
        appUsageTable_->insertRow(row);
        appUsageTable_->setItem(row, 0, new QTableWidgetItem(name));
        appUsageTable_->setItem(row, 1, new QTableWidgetItem(formatDuration(durationSec)));
        appUsageTable_->setItem(row, 2, new QTableWidgetItem(category));
        appUsageTable_->setItem(row, 3, new QTableWidgetItem(timestamp.left(19)));
    }
    setStatus(appUsageStatus_, tr("共 %1 条记录").arg(apps.size()));
    // 确保列宽设置正确（每次填充数据后重新调整）
    adjustColumnWidths();
    qDebug() << "[Console] populateAppUsage: populated" << apps.size() << "rows";
}

void ClientDetailsDialog::populateActivities(const QJsonArray& activities) {
    activityTable_->setRowCount(0);
    if (activities.isEmpty()) {
        setStatus(activityStatus_, tr("暂无活动日志"));
        return;
    }
    
    // 优化：按时间戳倒序排序，最新的在最前面
    QList<QJsonObject> sortedActivities;
    for (int i = 0; i < activities.size(); ++i) {
        sortedActivities.append(activities.at(i).toObject());
    }
    
    // 按时间戳倒序排序（最新的在前）
    std::sort(sortedActivities.begin(), sortedActivities.end(), [](const QJsonObject& a, const QJsonObject& b) {
        const QString timestampA = a.value(QStringLiteral("timestamp")).toString();
        const QString timestampB = b.value(QStringLiteral("timestamp")).toString();
        // ISO 8601格式可以直接字符串比较
        return timestampA > timestampB;
    });
    
    for (const QJsonObject& obj : sortedActivities) {
        const QString timestamp = obj.value(QStringLiteral("timestamp")).toString().left(19);
        const QString type = obj.value(QStringLiteral("activity_type")).toString(tr("未知"));
        const QJsonObject data = obj.value(QStringLiteral("data")).toObject();

        QString detail;
        if (type == QStringLiteral("window_change")) {
            // Support both old format (window_info) and new format (direct fields)
            const QJsonObject win = data.value(QStringLiteral("window_info")).toObject();
            QString windowTitle, appName;
            if (!win.isEmpty()) {
                // Old format
                windowTitle = win.value(QStringLiteral("title")).toString();
                appName = win.value(QStringLiteral("app")).toString();
            } else {
                // New format (direct fields)
                windowTitle = data.value(QStringLiteral("window_title")).toString();
                appName = data.value(QStringLiteral("app_name")).toString();
            }
            detail = tr("窗口: %1 | 应用: %2")
                         .arg(windowTitle.isEmpty() ? tr("未命名") : windowTitle)
                         .arg(appName.isEmpty() ? tr("未知") : appName);
        } else if (type == QStringLiteral("session_state")) {
            const bool locked = data.value(QStringLiteral("locked")).toBool(false);
            detail = locked ? tr("锁屏") : tr("解锁");
        } else if (!data.isEmpty()) {
            detail = QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
        }

        const int row = activityTable_->rowCount();
        activityTable_->insertRow(row);
        activityTable_->setItem(row, 0, new QTableWidgetItem(timestamp));
        activityTable_->setItem(row, 1, new QTableWidgetItem(type));
        activityTable_->setItem(row, 2, new QTableWidgetItem(detail));
    }
    setStatus(activityStatus_, tr("共 %1 条记录（最新在前）").arg(sortedActivities.size()));
    // 确保列宽设置正确（每次填充数据后重新调整）
    adjustColumnWidths();
}

void ClientDetailsDialog::populateScreenshots(const QJsonArray& screenshots) {
    screenshotTable_->setRowCount(0);
    if (screenshots.isEmpty()) {
        setStatus(screenshotStatus_, tr("暂无截图"));
        return;
    }

    for (int i = 0; i < screenshots.size(); ++i) {
        const QJsonObject obj = screenshots.at(i).toObject();
        const QString timestamp = obj.value(QStringLiteral("timestamp")).toString().left(19);
        const QString filename = obj.value(QStringLiteral("filename")).toString(
            obj.value(QStringLiteral("file")).toString());
        const bool isAlert = obj.value(QStringLiteral("is_alert")).toBool(false);
        const qint64 size = obj.value(QStringLiteral("size")).toVariant().toLongLong();

        const int row = screenshotTable_->rowCount();
        screenshotTable_->insertRow(row);
        screenshotTable_->setItem(row, 0, new QTableWidgetItem(timestamp));
        screenshotTable_->setItem(row, 1, new QTableWidgetItem(filename));
        screenshotTable_->setItem(row, 2, new QTableWidgetItem(isAlert ? tr("预警") : tr("常规")));
        screenshotTable_->setItem(row, 3,
                                  new QTableWidgetItem(tr("%1 KB").arg(size / 1024.0, 0, 'f', 1)));
    }
    setStatus(screenshotStatus_, tr("共 %1 条记录").arg(screenshots.size()));
    // 确保列宽设置正确（每次填充数据后重新调整）
    adjustColumnWidths();
    if (screenshotTable_->rowCount() > 0) {
        screenshotTable_->selectRow(0);
        handleScreenshotSelectionChanged();
    }
}

// 应用排行榜功能已移除

void ClientDetailsDialog::populateGlobalAlerts(const QJsonArray& alerts) {
    if (!alertTable_) {
        qWarning() << "[Console] populateGlobalAlerts: alertTable_ is null";
        return;
    }
    
    // 只在数据变化时输出 INFO 日志
    static int lastAlertCount = -1;
    const int currentAlertCount = alerts.size();
    const bool countChanged = (lastAlertCount != currentAlertCount);
    
    if (countChanged) {
        qInfo() << "[Console] populateGlobalAlerts: received" << alerts.size() << "alerts for clientId=" << clientId_;
        lastAlertCount = currentAlertCount;
    } else {
        qDebug() << "[Console] populateGlobalAlerts: refreshing" << alerts.size() << "alerts (no count change)";
    }
    
    alertTable_->setRowCount(0);
    if (alerts.isEmpty()) {
        if (countChanged) {
            qInfo() << "[Console] No alerts to display, setting status to '暂无预警'";
        }
        setStatus(alertStatus_, tr("暂无预警"), false);
        return;
    }

    for (int i = 0; i < alerts.size(); ++i) {
        const QJsonObject obj = alerts.at(i).toObject();
        const QString timestamp =
            obj.value(QStringLiteral("timestamp")).toString().left(19);
        const QString keyword = obj.value(QStringLiteral("keyword")).toString();
        const QString window = obj.value(QStringLiteral("window_title")).toString(
            obj.value(QStringLiteral("window")).toString());
        const QString type = obj.value(QStringLiteral("alert_type")).toString();
        QString context = obj.value(QStringLiteral("context")).toString();
        if (context.size() > 120) {
            context = context.left(117) + QStringLiteral("...");
        }
        const QString screenshot = obj.value(QStringLiteral("screenshot")).toString();

        // 降级为 DEBUG：每行的详细日志在自动刷新时会产生大量冗余输出
        qDebug() << "[Console] Adding alert row" << i << ": keyword=" << keyword 
                << "timestamp=" << timestamp << "window=" << window;

        const int row = alertTable_->rowCount();
        alertTable_->insertRow(row);
        auto* tsItem = new QTableWidgetItem(timestamp);
        if (!screenshot.isEmpty()) {
            tsItem->setData(Qt::UserRole, screenshot);
        }
        alertTable_->setItem(row, 0, tsItem);
        alertTable_->setItem(row, 1, new QTableWidgetItem(keyword));
        alertTable_->setItem(row, 2, new QTableWidgetItem(window));
        alertTable_->setItem(row, 3, new QTableWidgetItem(type));
        alertTable_->setItem(row, 4, new QTableWidgetItem(context));
    }
    
    // 只在数据变化时输出 INFO 日志
    if (countChanged) {
        qInfo() << "[Console] populateGlobalAlerts: inserted" << alerts.size() << "rows into alertTable_";
    }
    setStatus(alertStatus_, tr("共 %1 条预警").arg(alerts.size()));
    // 确保列宽设置正确（每次填充数据后重新调整）
    adjustColumnWidths();
}

void ClientDetailsDialog::setStatus(QLabel* label, const QString& text, bool isError) {
    if (!label) {
        return;
    }
    label->setText(text);
    if (isError) {
        label->setStyleSheet(QStringLiteral("color: #f87171;"));
    } else {
        label->setStyleSheet(QStringLiteral("color: #9ca3af;"));
    }
}

void ClientDetailsDialog::handleActivityDoubleClicked(QTableWidgetItem* item) {
    if (!item || !activityTable_ || !tabs_) {
        return;
    }
    const int row = item->row();
    if (row < 0 || row >= activityTable_->rowCount()) {
        return;
    }
    const QTableWidgetItem* tsItem = activityTable_->item(row, 0);
    if (!tsItem) {
        return;
    }
    const QString timestamp = tsItem->text();
    if (timestamp.isEmpty()) {
        return;
    }
    if (screenshotPage_) {
        tabs_->setCurrentWidget(screenshotPage_);
    }
    focusScreenshotByTimestamp(timestamp);
}

void ClientDetailsDialog::handleAlertDoubleClicked(QTableWidgetItem* item) {
    if (!item || !alertTable_ || !tabs_) {
        return;
    }
    const int row = item->row();
    if (row < 0 || row >= alertTable_->rowCount()) {
        return;
    }
    QString screenshotName;
    QString timestamp;
    QTableWidgetItem* tsItem = alertTable_->item(row, 0);
    if (tsItem) {
        screenshotName = tsItem->data(Qt::UserRole).toString();
        timestamp = tsItem->text();
    }
    if (screenshotPage_) {
        tabs_->setCurrentWidget(screenshotPage_);
    }
    if (!screenshotName.isEmpty()) {
        focusScreenshotByFilename(screenshotName);
    } else if (!timestamp.isEmpty()) {
        focusScreenshotByTimestamp(timestamp);
    }
}

void ClientDetailsDialog::handleScreenshotSelectionChanged() {
    resetScreenshotPreview();
    const auto selection = screenshotTable_->selectedItems();
    if (selection.isEmpty()) {
        return;
    }
    const int row = selection.first()->row();
    const QString filename = screenshotTable_->item(row, 1)->text();
    const QString timestamp = screenshotTable_->item(row, 0)->text();
    currentScreenshotFilename_.clear();
    currentScreenshotBytes_.clear();
    
    // 完全直连模式：优先从MainWindow缓存获取截图数据
    if (mainWindow_) {
        const QMap<QString, QByteArray> screenshots = mainWindow_->getClientScreenshots(clientId_);
        // 尝试匹配timestamp（可能格式不完全一致，需要灵活匹配）
        for (auto it = screenshots.begin(); it != screenshots.end(); ++it) {
            QString cachedTimestamp = it.key();
            // 移除时区信息进行比较（例如 "2025-11-13T20:02:42Z" vs "2025-11-13T20:02:42"）
            QString normalizedCached = cachedTimestamp.left(19);
            QString normalizedSelected = timestamp.left(19);
            if (normalizedCached == normalizedSelected || cachedTimestamp.contains(timestamp) || timestamp.contains(normalizedCached)) {
                // 找到匹配的截图数据，直接显示
                updateScreenshotPreview(it.value(), filename);
                qInfo() << "[Console] Screenshot preview loaded from MainWindow cache, timestamp:" << cachedTimestamp;
                return;
            }
        }
        qInfo() << "[Console] Screenshot not found in MainWindow cache, requesting from server, timestamp:" << timestamp;
    }
    
    // 兼容模式：从服务器获取截图
    requestScreenshotPreview(filename);
}

void ClientDetailsDialog::handleScreenshotOpen() {
    if (currentScreenshotFilename_.isEmpty() || currentScreenshotBytes_.isEmpty()) {
        return;
    }
    const QString tempDir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/qtconsole");
    QDir().mkpath(tempDir);
    const QString filePath = tempDir + QLatin1Char('/') + currentScreenshotFilename_;
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(currentScreenshotBytes_);
        file.close();
        QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
    } else {
        QMessageBox::warning(this, tr("错误"), tr("无法写入临时文件：%1").arg(file.errorString()));
    }
}

void ClientDetailsDialog::handleScreenshotSave() {
    if (currentScreenshotFilename_.isEmpty() || currentScreenshotBytes_.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先选择一张截图"));
        return;
    }
    const QString filePath = QFileDialog::getSaveFileName(
        this, tr("保存截图"), currentScreenshotFilename_,
        tr("图像文件 (*.jpg *.png *.jpeg *.bmp);;所有文件 (*.*)"));
    if (filePath.isEmpty()) {
        return;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("错误"), tr("保存失败：%1").arg(file.errorString()));
        return;
    }
    file.write(currentScreenshotBytes_);
    file.close();
    QMessageBox::information(this, tr("成功"), tr("截图已保存到 %1").arg(filePath));
}

void ClientDetailsDialog::handleScreenshotDelete() {
    const auto selection = screenshotTable_->selectedItems();
    if (selection.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先选择一张截图"));
        return;
    }
    const int row = selection.first()->row();
    const QString filename = screenshotTable_->item(row, 1)->text();
    const auto confirm = QMessageBox::question(
        this, tr("删除确认"), tr("确定要删除截图 %1 吗？该操作不可恢复。").arg(filename));
    if (confirm != QMessageBox::Yes) {
        return;
    }
    requestScreenshotDelete(filename);
}

void ClientDetailsDialog::updateScreenshotPreview(const QByteArray& bytes, const QString& filename) {
    screenshotPreviewLoading_ = false;
    if (bytes.isEmpty()) {
        screenshotPreview_->setText(tr("无法加载截图"));
        setStatus(screenshotStatus_, tr("加载失败"), true);
        return;
    }
    QPixmap pixmap;
    if (!pixmap.loadFromData(bytes)) {
        screenshotPreview_->setText(tr("无法解析图像"));
        setStatus(screenshotStatus_, tr("解析失败"), true);
        return;
    }
    currentScreenshotBytes_ = bytes;
    currentScreenshotFilename_ = filename;
    screenshotPreview_->setPixmap(pixmap.scaled(
        screenshotPreview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    screenshotOpen_->setEnabled(true);
    screenshotSave_->setEnabled(true);
    screenshotDelete_->setEnabled(true);
    setStatus(screenshotStatus_, tr("载入完毕"));
}

void ClientDetailsDialog::resetScreenshotPreview() {
    screenshotPreviewLoading_ = false;
    currentScreenshotFilename_.clear();
    currentScreenshotBytes_.clear();
    if (screenshotPreview_) {
        screenshotPreview_->setText(tr("请选择一张截图"));
        screenshotPreview_->setPixmap(QPixmap());
    }
    if (screenshotOpen_) {
        screenshotOpen_->setEnabled(false);
    }
    if (screenshotSave_) {
        screenshotSave_->setEnabled(false);
    }
    if (screenshotDelete_) {
        screenshotDelete_->setEnabled(false);
    }
}

void ClientDetailsDialog::focusScreenshotByFilename(const QString& filename) {
    if (!screenshotTable_ || filename.isEmpty()) {
        return;
    }
    for (int row = 0; row < screenshotTable_->rowCount(); ++row) {
        QTableWidgetItem* fileItem = screenshotTable_->item(row, 1);
        if (fileItem && fileItem->text().compare(filename, Qt::CaseInsensitive) == 0) {
            screenshotTable_->selectRow(row);
            screenshotTable_->scrollToItem(fileItem, QAbstractItemView::PositionAtCenter);
            setStatus(screenshotStatus_, tr("已定位到截图：%1").arg(filename));
            handleScreenshotSelectionChanged();
            return;
        }
    }
    setStatus(screenshotStatus_, tr("未找到对应截图，尝试按时间匹配"), true);
}

void ClientDetailsDialog::focusScreenshotByTimestamp(const QString& timestamp) {
    if (!screenshotTable_ || screenshotTable_->rowCount() == 0) {
        return;
    }
    QDateTime target = QDateTime::fromString(timestamp, Qt::ISODate);
    if (!target.isValid()) {
        target = QDateTime::fromString(timestamp, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    if (!target.isValid()) {
        return;
    }

    int bestRow = -1;
    qint64 bestDiff = std::numeric_limits<qint64>::max();
    for (int row = 0; row < screenshotTable_->rowCount(); ++row) {
        QTableWidgetItem* tsItem = screenshotTable_->item(row, 0);
        if (!tsItem) {
            continue;
        }
        QString rowTsStr = tsItem->text();
        QDateTime rowTs = QDateTime::fromString(rowTsStr, Qt::ISODate);
        if (!rowTs.isValid()) {
            rowTs = QDateTime::fromString(rowTsStr, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        }
        if (!rowTs.isValid()) {
            continue;
        }
        qint64 diff = std::llabs(rowTs.msecsTo(target));
        if (diff < bestDiff) {
            bestDiff = diff;
            bestRow = row;
        }
    }

    if (bestRow >= 0) {
        screenshotTable_->selectRow(bestRow);
        QTableWidgetItem* item = screenshotTable_->item(bestRow, 0);
        if (item) {
            screenshotTable_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        }
        setStatus(screenshotStatus_,
                  tr("已定位到最接近 %1 的截图").arg(target.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    }
}

void ClientDetailsDialog::handleWorkHoursSave() {
    const QString startTime = workStartTime_->text().trimmed();
    const QString endTime = workEndTime_->text().trimmed();
    
    if (startTime.isEmpty() || endTime.isEmpty()) {
        QMessageBox::warning(this, tr("错误"), tr("请输入开始时间和结束时间"));
        return;
    }
    
    // Validate time format (HH:mm)
    QTime start = QTime::fromString(startTime, "HH:mm");
    QTime end = QTime::fromString(endTime, "HH:mm");
    if (!start.isValid() || !end.isValid()) {
        QMessageBox::warning(this, tr("错误"), tr("时间格式错误，请使用 HH:mm 格式（例如: 09:00）"));
        return;
    }
    
    // Send command to client via CommandController
    QJsonObject payload;
    payload.insert(QStringLiteral("action"), QStringLiteral("work_hours"));
    payload.insert(QStringLiteral("start_time"), startTime);
    payload.insert(QStringLiteral("end_time"), endTime);
    
    sendClientCommand(payload, RequestKind::WorkHoursSave);
}

void ClientDetailsDialog::handlePauseClient() {
    QJsonObject payload;
    payload.insert(QStringLiteral("action"), QStringLiteral("pause"));
    sendClientCommand(payload, RequestKind::ClientControl);
    QMessageBox::information(this, tr("提示"), tr("已发送暂停命令到客户端"));
}

void ClientDetailsDialog::handleResumeClient() {
    QJsonObject payload;
    payload.insert(QStringLiteral("action"), QStringLiteral("resume"));
    sendClientCommand(payload, RequestKind::ClientControl);
    QMessageBox::information(this, tr("提示"), tr("已发送恢复命令到客户端"));
}

void ClientDetailsDialog::handleUploadNow() {
    QJsonObject payload;
    payload.insert(QStringLiteral("action"), QStringLiteral("upload_now"));
    sendClientCommand(payload, RequestKind::ClientControl);
    QMessageBox::information(this, tr("提示"), tr("已发送立即上传命令到客户端"));
}

void ClientDetailsDialog::handleUnlockScreen() {
    // 询问用户是否需要输入密码
    bool ok;
    QString password = QInputDialog::getText(this, tr("解锁锁屏"), 
                                              tr("请输入密码（留空则尝试无密码解锁）："), 
                                              QLineEdit::Password, QString(), &ok);
    
    if (!ok) {
        return;  // 用户取消
    }
    
    QJsonObject payload;
    payload.insert(QStringLiteral("action"), QStringLiteral("unlock_screen"));
    if (!password.isEmpty()) {
        payload.insert(QStringLiteral("password"), password);
    }
    sendClientCommand(payload, RequestKind::ClientControl);
    QMessageBox::information(this, tr("提示"), tr("已发送解锁锁屏命令到客户端"));
}

void ClientDetailsDialog::sendClientCommand(const QJsonObject& payload, RequestKind kind) {
    // 纯UDP模式：暂不支持主动发送命令到客户端
    // 客户端只发送心跳和数据，不接受DesktopConsole的命令
    Q_UNUSED(payload);
    Q_UNUSED(kind);
    QMessageBox::information(this, tr("提示"), 
        tr("纯UDP模式暂不支持主动发送命令到客户端\n"
           "工作时间、暂停/恢复等功能需要在客户端配置"));
}

void ClientDetailsDialog::setupAutoRefresh() {
    // 创建自动刷新定时器，每5秒刷新一次所有数据
    autoRefreshTimer_ = new QTimer(this);
    autoRefreshTimer_->setInterval(5000);  // 5秒刷新一次
    connect(autoRefreshTimer_, &QTimer::timeout, this, [this]() {
        // 只刷新当前可见的标签页数据，避免不必要的网络请求
        if (!tabs_) {
            return;
        }
        const int currentIndex = tabs_->currentIndex();
        qDebug() << "[Console] Auto-refresh triggered, current tab index:" << currentIndex;
        if (currentIndex == 0 && appUsagePage_) {
            // 应用统计标签页
            qDebug() << "[Console] Auto-refreshing app usage";
            loadAppUsage();
        } else if (currentIndex == 1 && activityPage_) {
            // 活动日志标签页
            qDebug() << "[Console] Auto-refreshing activities";
            loadActivities();
        } else if (currentIndex == 2 && screenshotPage_) {
            // 截图标签页
            qDebug() << "[Console] Auto-refreshing screenshots";
            loadScreenshots();
        } else if (currentIndex == 3 && alertPage_) {
            // 敏感词预警标签页
            qDebug() << "[Console] Auto-refreshing alerts";
            loadGlobalAlerts();
        }
    });
    autoRefreshTimer_->start();
    qInfo() << "[Console] Auto-refresh timer started, interval: 5000ms";
}

void ClientDetailsDialog::adjustColumnWidths() {
    // 调整活动日志表格列宽：时间列加宽
    if (activityTable_) {
        activityTable_->setColumnWidth(0, 180);  // 时间列：180像素（足够显示完整时间戳）
        activityTable_->setColumnWidth(1, 120);  // 类型列：120像素
        // 详情列自动拉伸
    }
    
    // 调整截图表格列宽：时间列加宽
    if (screenshotTable_) {
        screenshotTable_->setColumnWidth(0, 180);  // 时间列：180像素
        screenshotTable_->setColumnWidth(1, 200);  // 文件名列：200像素
        screenshotTable_->setColumnWidth(2, 80);   // 类别列：80像素
        screenshotTable_->setColumnWidth(3, 80);   // 大小列：80像素
    }
    
    // 调整报警表格列宽：时间列加宽
    if (alertTable_) {
        alertTable_->setColumnWidth(0, 180);  // 时间列：180像素
        alertTable_->setColumnWidth(1, 100);  // 关键词列：100像素
        alertTable_->setColumnWidth(2, 200);  // 窗口/应用列：200像素
        alertTable_->setColumnWidth(3, 80);   // 类型列：80像素
        // 上下文列自动拉伸
    }
    
    // 调整应用统计表格列宽：最后使用列（时间）加宽
    if (appUsageTable_) {
        appUsageTable_->setColumnWidth(3, 180);  // 最后使用列（时间）：180像素
    }
}

void ClientDetailsDialog::createWindowScreenshotConfigDialog() {
    if (windowScreenshotConfigDialog_) {
        return;  // 对话框已创建
    }
    
    windowScreenshotConfigDialog_ = new QDialog(this);
    windowScreenshotConfigDialog_->setWindowTitle(tr("窗口变更截图应用配置 - %1").arg(displayName_));
    windowScreenshotConfigDialog_->setMinimumSize(500, 400);
    windowScreenshotConfigDialog_->setStyleSheet(QStringLiteral(
        "QDialog { background-color: #0f172a; }"));
    
    auto* mainLayout = new QVBoxLayout(windowScreenshotConfigDialog_);
    mainLayout->setSpacing(12);
    
    auto* infoLabel = new QLabel(tr("配置需要窗口变更时自动截图的应用列表："));
    infoLabel->setStyleSheet(QStringLiteral("color: #e2e8f0; padding: 8px;"));
    mainLayout->addWidget(infoLabel);
    
    windowScreenshotAppsList_ = new QListWidget();
    windowScreenshotAppsList_->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: #0f172a; color: #e2e8f0; border: 1px solid #1e293b; }"
        "QListWidget::item:selected { background-color: #3b82f6; color: white; }"
        "QListWidget::item:hover { background-color: #1e293b; }"));
    mainLayout->addWidget(windowScreenshotAppsList_, 1);
    
    auto* controlLayout = new QHBoxLayout();
    windowScreenshotAppEntry_ = new QLineEdit();
    windowScreenshotAppEntry_->setPlaceholderText(tr("输入应用名称（例如: Telegram, Chrome）"));
    windowScreenshotAppEntry_->setStyleSheet(QStringLiteral(
        "QLineEdit { background-color: #1e293b; color: #e2e8f0; border: 1px solid #334155; padding: 4px; }"));
    controlLayout->addWidget(windowScreenshotAppEntry_);
    
    windowScreenshotAppAdd_ = new QPushButton(tr("➕ 添加"));
    windowScreenshotAppAdd_->setCursor(Qt::PointingHandCursor);
    controlLayout->addWidget(windowScreenshotAppAdd_);
    
    windowScreenshotAppRemove_ = new QPushButton(tr("➖ 删除"));
    windowScreenshotAppRemove_->setCursor(Qt::PointingHandCursor);
    controlLayout->addWidget(windowScreenshotAppRemove_);
    
    mainLayout->addLayout(controlLayout);
    
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    windowScreenshotAppSave_ = new QPushButton(tr("💾 保存配置"));
    windowScreenshotAppSave_->setCursor(Qt::PointingHandCursor);
    buttonLayout->addWidget(windowScreenshotAppSave_);
    
    auto* closeButton = new QPushButton(tr("关闭"));
    closeButton->setCursor(Qt::PointingHandCursor);
    buttonLayout->addWidget(closeButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // 连接信号
    connect(windowScreenshotAppAdd_, &QPushButton::clicked, this, &ClientDetailsDialog::handleWindowScreenshotAppAdd);
    connect(windowScreenshotAppRemove_, &QPushButton::clicked, this, &ClientDetailsDialog::handleWindowScreenshotAppRemove);
    connect(windowScreenshotAppSave_, &QPushButton::clicked, this, &ClientDetailsDialog::handleWindowScreenshotAppSave);
    connect(windowScreenshotAppEntry_, &QLineEdit::returnPressed, this, &ClientDetailsDialog::handleWindowScreenshotAppAdd);
    connect(closeButton, &QPushButton::clicked, windowScreenshotConfigDialog_, &QDialog::accept);
    
    // 加载配置
    loadWindowScreenshotApps();
}

void ClientDetailsDialog::handleWindowScreenshotConfigButtonClicked() {
    if (!windowScreenshotConfigDialog_) {
        createWindowScreenshotConfigDialog();
    }
    // 重新加载配置（确保显示最新数据）
    loadWindowScreenshotApps();
    windowScreenshotConfigDialog_->exec();
}

void ClientDetailsDialog::loadWindowScreenshotApps() {
    // 纯UDP模式：从数据库读取窗口截图应用配置
    // 注意：此功能需要在数据库中添加 window_screenshot_apps 表
    // 暂时显示提示信息
    if (windowScreenshotAppsList_) {
        windowScreenshotAppsList_->clear();
        // TODO: 从数据库读取配置
        // windowScreenshotAppsList_->addItem("示例应用.exe");
    }
}

void ClientDetailsDialog::handleWindowScreenshotAppAdd() {
    if (!windowScreenshotAppEntry_ || !windowScreenshotAppsList_) {
        return;
    }
    const QString appName = windowScreenshotAppEntry_->text().trimmed();
    if (appName.isEmpty()) {
        return;
    }
    
    // 检查是否已存在
    for (int i = 0; i < windowScreenshotAppsList_->count(); ++i) {
        if (windowScreenshotAppsList_->item(i)->text() == appName) {
            QMessageBox::information(windowScreenshotConfigDialog_ ? windowScreenshotConfigDialog_ : this, 
                                    tr("提示"), tr("应用 \"%1\" 已存在").arg(appName));
            return;
        }
    }
    
    windowScreenshotAppsList_->addItem(appName);
    windowScreenshotAppEntry_->clear();
}

void ClientDetailsDialog::handleWindowScreenshotAppRemove() {
    if (!windowScreenshotAppsList_) {
        return;
    }
    QListWidgetItem* item = windowScreenshotAppsList_->currentItem();
    if (!item) {
        QMessageBox::information(windowScreenshotConfigDialog_ ? windowScreenshotConfigDialog_ : this, 
                                tr("提示"), tr("请先选择要删除的应用"));
        return;
    }
    
    delete item;
}

void ClientDetailsDialog::handleWindowScreenshotAppSave() {
    if (!windowScreenshotAppsList_) {
        return;
    }
    QStringList apps;
    for (int i = 0; i < windowScreenshotAppsList_->count(); ++i) {
        apps.append(windowScreenshotAppsList_->item(i)->text());
    }
    
    // 纯UDP模式：保存到数据库
    // TODO: 需要在数据库中添加 window_screenshot_apps 表
    // 目前只显示提示
    QMessageBox::information(windowScreenshotConfigDialog_ ? windowScreenshotConfigDialog_ : this,
        tr("提示"), 
        tr("纯UDP模式暂不支持窗口截图应用配置\n"
           "此功能需要在客户端配置文件中设置"));
}

}  // namespace console

