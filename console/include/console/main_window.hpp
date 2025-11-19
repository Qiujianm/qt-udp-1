#pragma once

#include <QMainWindow>
#include <QMap>
#include <QPointer>
#include <memory>
#include <QVector>
#include <QMetaObject>
#include <QSet>
#include <QString>
#include <QComboBox>
#include <QComboBox>
#include <QPushButton>
#include <QProcess>
#include <QQueue>
#include <QUdpSocket>
#include <QSqlDatabase>
#include <QTimer>

#include "core/app_config.hpp"
#include "network/ws_channel.hpp"
#include "console/client_discovery.hpp"
#include "console/jpeg_receiver.hpp"  // 纯UDP视频接收
// 完全直连方案：不需要ConsoleControlServer和ConsoleBroadcaster
// #include "console/console_control_server.hpp"
// #include "console/console_broadcaster.hpp"

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QGridLayout;
class QSplitter;
class QJsonArray;
class QAction;
class QScrollArea;
class QNetworkAccessManager;
class QNetworkReply;

namespace console {

static constexpr int kItemTypeGroup = 1;
static constexpr int kItemTypeClient = 2;
static constexpr int kRoleType = Qt::UserRole;
static constexpr int kRoleClientId = Qt::UserRole + 1;
static constexpr int kRoleGroupName = Qt::UserRole + 2;

class StreamTile;
class StreamPlayer;
class FullscreenView;
class ClientTreeWidget;
class ClientDetailsDialog;
class PlaceholderTile;
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    
    // 完全直连模式：供ClientDetailsDialog访问客户端数据
    QJsonArray getClientAppUsage(const QString& clientId) const;
    QJsonArray getClientActivities(const QString& clientId) const;
    QMap<QString, QByteArray> getClientScreenshots(const QString& clientId) const;  // timestamp -> jpeg data
    QStringList loadSensitiveWords();  // 供ClientDetailsDialog加载敏感词列表

private slots:
    void handleStatusChanged(const QString& status);
    void handleClientItemActivated(QTreeWidgetItem* item, int column);
    void handleClientContextMenu(const QPoint& pos);
    void handleTileContextMenu(StreamTile* tile, const QPoint& globalPos);
    void handlePreviewContextMenu(const QPoint& pos);
    void handleClientDropped(const QString& clientId, const QString& newGroup);

private:
    void setupUi();
    void setupControlChannel();
    void handleControlText(const QString& payload);
    void requestClientList();
    void refreshClientModel(const QJsonArray& items);
    void fetchClientsFromRestApi();
    void handleRestApiReply(QNetworkReply* reply);
    void saveClientRemarkToRestApi(const QString& clientId, const QString& remark);
    void startPreview(const QString& clientId, quint32 ssrc);
    void stopPreview(const QString& clientId);
    void removePreview(const QString& clientId);
    void rebuildPreviewLayout();
    void sendSubscribe(const QString& clientId, quint32 ssrc, quint16 port);
    void sendUnsubscribe(const QString& clientId, quint32 ssrc, quint16 port);
    void sendDirectSubscribe(const QString& clientId, quint32 ssrc, quint16 port);
    void sendDirectUnsubscribe(const QString& clientId, quint32 ssrc, quint16 port);
    void handleDirectClientMessage(const QString& clientId, const QString& message);
    void handleDirectClientBinary(const QString& clientId, const QByteArray& data);
    void updateClientTreeItem(const QString& clientId);
    void rebuildToolbar();
    void handleTileDropped(const QString& targetId, const QString& sourceId);
    void updateTileDragEnabled();
    void applyLayoutPreset(const QString& presetId);
    void applyLayoutPresetFromAction();
    void updateLayoutActions();
    void toggleWallFullscreen();
    void setWallFullscreen(bool enable);
    void updateTileStats(const QString& clientId, double fps, double mbps, const QString& errorText);
    void openFullscreenView(const QString& clientId);
    void addGroup();
    void renameGroup(QTreeWidgetItem* groupItem);
    void removeGroup(QTreeWidgetItem* groupItem);
    void editClientRemark(const QString& clientId);
    void openClientDetails(const QString& clientId);
    void handleGroupFilterChanged(int index);
    void schedulePreviewRelayout();
    void handleClearAllData();
    void handleSaveTimeSettings();
    void handleViewVideoRecords(const QString& clientId);
    void openVideoPlayer(const QString& videoPath);
    void openVideoPlayerWithFFplay(const QString& videoPath, const QString& ffplayPath);
    void performWebSocketReconnect(const DiscoveredClient& client);
    void initializeServices();
    void startService(int index);
    void stopServices();
    void handleServiceFinished(int index, int exitCode, QProcess::ExitStatus status);
    void ensureQtRuntimeArtifacts();
    static bool ensureFileCopied(const QString& source, const QString& destination);
    static bool ensureDirectory(const QString& path);
    QString detectDllDirectory() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    static QString DefaultGroup();

    core::AppConfig config_;
    std::unique_ptr<network::WsChannel> controlChannel_;
    std::unique_ptr<ClientDiscovery> clientDiscovery_;  // UDP客户端发现（完全直连方案）
    // 完全直连方案：DesktopConsole作为客户端连接到StreamClient，不需要自己的WebSocket服务器
    // std::unique_ptr<ConsoleControlServer> consoleControlServer_;  // 已禁用（避免端口冲突）
    // std::unique_ptr<ConsoleBroadcaster> consoleBroadcaster_;  // 已禁用（不需要）
    QNetworkAccessManager* restApiManager_ = nullptr;
    ClientTreeWidget* clientTree_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* metricsLabel_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QWidget* previewContainer_ = nullptr;
    QGridLayout* previewLayout_ = nullptr;
    QScrollArea* previewScrollArea_ = nullptr;
    QWidget* wallHeader_ = nullptr;
    QLabel* wallStatsLabel_ = nullptr;
    QPushButton* saveTimeButton_ = nullptr;
    QComboBox* groupFilterCombo_ = nullptr;
    QString currentGroupFilter_{QStringLiteral("ALL")};
    struct ClientEntry {
        QString id;           // 客户端ID
        QString hostname;     // 主机名
        QString username;     // 用户名
        QString ip;           // IP地址
        quint32 ssrc{0};
        bool online{false};
        QString group;
        QString remark;
        QDateTime lastSeen;   // 最后在线时间
    };
    QMap<QString, ClientEntry> clientEntries_;          // client_id -> info
    QMap<QString, QTreeWidgetItem*> clientItems_;       // client_id -> item
    QMap<QString, StreamTile*> activeTiles_;      // client_id -> tile
    QMap<QString, StreamPlayer*> activePlayers_;  // client_id -> player
    QHash<QString, network::WsChannel*> directControlChannels_;  // 完全直连：每个客户端的WebSocket连接
    QHash<QString, QTimer*> reconnectTimers_;  // 重连防抖定时器
    QSet<QString> connectingClients_;  // 正在连接中的客户端（避免重复连接）
    struct StreamStats {
        double fps{0.0};
        double mbps{0.0};
    };
    QMap<QString, StreamStats> tileStats_;
    
    // 视频流保存设置
    int videoSaveDurationHours_{24};  // 默认保存24小时
    QString videoSavePath_;  // 保存路径
    QSet<QString> recordingDisabledClients_;  // 被手动停止录制的客户端列表
    
    int gridColumns_{4};
    int targetRows_{1};
    bool layoutLocked_{false};
    QVector<QString> layoutOrder_;
    QAction* lockLayoutAction_{nullptr};
    QVector<QAction*> layoutPresetActions_;
    QString currentLayoutPreset_{QStringLiteral("1x4")};
    QString lastGridPreset_{QStringLiteral("1x4")};
    bool wallFullscreen_{false};
    QRect normalGeometry_;
    Qt::WindowStates normalWindowState_{Qt::WindowNoState};
    QAction* wallFullscreenAction_{nullptr};
    QString wallFullscreenReturnPreset_;
    int wallFullscreenReturnTargetRows_{1};
    bool clientTreeVisibleBeforeWall_{true};
    QMap<QString, QString> lastErrorTexts_;
    QString lastErrorMessage_;
    QPointer<FullscreenView> activeFullscreen_;
    QVector<QMetaObject::Connection> activeFullscreenConnections_;
    QPointer<ClientDetailsDialog> activeDetailsDialog_;
    QVector<PlaceholderTile*> placeholderTiles_;
    bool layoutRefreshPending_{false};
    int previewSpacingNormal_{8};
    QMargins previewMarginsNormal_{8, 8, 8, 8};
    struct ManagedService {
        QString name;
        QString executable;
        QStringList arguments;
        bool autoRestart{true};
        quint16 listenPort{0};
        QProcess* process{nullptr};
        int restartAttempts{0};
        QByteArray pendingOutput;
        bool requestedStop{false};
    };
    QVector<ManagedService> services_;
    bool shuttingDown_{false};

    QMap<QString, QTreeWidgetItem*> groupItems_;
    QSet<QString> groupNames_;
    QMap<QString, QString> clientRemarksCache_;
    QMap<QString, QString> clientGroupsCache_;
    QString metadataPath_;
    QTimer* statusBarUpdateTimer_{nullptr};  // 状态栏更新定时器（避免频繁更新）
    QTimer* clientTreeRebuildTimer_{nullptr};  // 客户端树重建定时器（批量更新，避免频繁重建）
    bool clientTreeNeedsRebuild_{false};  // 标记是否需要重建客户端树
    
    // 完全直连模式：存储从StreamClient接收的数据
    QMap<QString, QJsonArray> clientAppUsageData_;  // clientId -> app usage array
    QMap<QString, QJsonArray> clientActivitiesData_;  // clientId -> activities array
    QMap<QString, QMap<QString, QByteArray>> clientScreenshotsData_;  // clientId -> (timestamp -> jpeg data)
    QMap<QString, QQueue<QString>> pendingScreenshotMetadata_;  // clientId -> queue of screenshot metadata JSON (修复：使用队列避免覆盖)

    // 集成 CommandController 功能 (纯UDP架构)
    QUdpSocket* udpReceiver_{nullptr};  // UDP 10000 接收器（控制消息）
    JpegReceiver* videoReceiver_{nullptr};  // UDP 5004 接收器（视频流）
    QSqlDatabase db_;  // 集成数据库
    QString dataDir_;  // 数据目录
    QString dbPath_;  // 数据库路径
    QString screenshotDir_;  // 截图目录
    QString alertsDir_;  // 告警截图目录
    QString dbConnectionName_;  // 数据库连接名
    bool databaseInitialized_{false};
    QMap<QString, QDateTime> clientLastHeartbeat_;  // 客户端心跳时间
    QTimer* heartbeatCheckTimer_{nullptr};  // 心跳超时检查定时器
    QStringList sensitiveWords_;  // 敏感词列表

    // 集成 CommandController 的方法
    void handleUdpDatagram();
    void handleHeartbeat(const QJsonObject& obj, const QHostAddress& sender, quint16 port);
    void handleAlert(const QByteArray& data);
    void handleActivities(const QJsonObject& obj);
    void handleAppUsage(const QJsonObject& obj);
    
    // 视频流处理
    void handleVideoFrame(quint32 ssrc, quint32 frameId, const QByteArray& jpegData);
    void updateVideoTile(const QString& clientId, const QByteArray& jpegData);
    QString findClientBySSRC(quint32 ssrc) const;
    void handleWindowChange(const QJsonObject& obj);
    bool initDatabase();
    bool ensureDatabase();
    void sendHeartbeatAck(const QString& clientId, const QHostAddress& address, quint16 port);
    void checkClientHeartbeats();
    void insertAlertRecord(const QString& clientId, const QJsonObject& alertObj);
    void updateClientRecord(const QString& clientId, const QString& hostname, const QString& ipAddress,
                           const QString& osInfo, const QString& username, const QString& status);
    QString saveScreenshotFileDirect(const QString& clientId, const QByteArray& data,
                                     const QString& timestamp, bool isAlert, QString* isoTimestampOut = nullptr);
    void sendSensitiveWordsUpdate(const QString& clientId, const QHostAddress& address, quint16 port);
    void broadcastSensitiveWordsUpdateViaUdp();
    void sendUdpMessage(const QJsonObject& message, const QHostAddress& address, quint16 port);

    void appendLayoutActions(QMenu* menu);
    void updateStatusBarStats();
    void updateWallHeaderStats();
    void populateGroupFilterOptions();
    QStringList orderedClientIdsForFilter(const QString& filter) const;
    void syncPreviewWithFilter();
    QString computeDisplayName(const QString& clientId) const;
    void loadClientMetadata();
    void saveClientMetadata() const;
    QTreeWidgetItem* ensureGroupItem(const QString& groupName);
    void scheduleClientTreeRebuild();  // 批量更新客户端树
    void rebuildClientTree();  // 实际重建（由定时器调用）
    QString groupForClient(const QString& clientId) const;
    QString remarkForClient(const QString& clientId) const;
    void updateTileDisplayName(const QString& clientId);
};

}  // namespace console
