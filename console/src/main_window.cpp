#include <QLibraryInfo>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include "console/main_window.hpp"
#include "console/client_details_dialog.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QGridLayout>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QLayoutItem>
#include <QPainter>
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QComboBox>
#include <QSignalBlocker>
#include <QComboBox>
#include <QProcess>
#include <QTcpServer>
#include <QThread>
#include <QLibraryInfo>
#include <QProcessEnvironment>
#include <QFileInfo>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QtEndian>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <QVariant>
#include <QSet>
#include <QKeyEvent>
#include <QElapsedTimer>
#include <QScrollBar>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QPixmap>
#include <QResizeEvent>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QSlider>
#include <QTime>
#include <QTimeEdit>
#include <QAudioOutput>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
}
#include <QCloseEvent>
#include <QPointer>
#include <QScreen>
#include <QInputDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDialog>
#include <QGroupBox>
#include <QSpinBox>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QUrlQuery>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QNetworkDatagram>
#include <vector>

#include <memory>
#include <optional>
#include <stdexcept>
#include <cstring>
#include <QVector>
#include <cmath>
#include <algorithm>

namespace {

constexpr int kDefaultGridColumns = 3;
constexpr QColor kTileBackground(30, 30, 30);
constexpr QColor kTileBorder(70, 70, 70);
constexpr quint32 kJpegMagic = 0x4a503031;  // "JP01"
constexpr int kJpegHeaderSize = 20;

#if defined(Q_OS_WIN)
void terminateProcessIfRunning(const QString& executableName) {
    if (executableName.isEmpty()) {
        return;
    }
    QProcess::execute(QStringLiteral("taskkill"),
                      {QStringLiteral("/F"), QStringLiteral("/T"), QStringLiteral("/IM"), executableName});
}
#else
void terminateProcessIfRunning(const QString&) {}
#endif

}  // namespace

namespace console {

class ClientTreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    explicit ClientTreeWidget(QWidget* parent = nullptr) : QTreeWidget(parent) {
        setHeaderHidden(true);
        setRootIsDecorated(true);
        setExpandsOnDoubleClick(false);
        setItemsExpandable(true);
        setAnimated(true);
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::InternalMove);
        setDefaultDropAction(Qt::MoveAction);
        setSelectionMode(QAbstractItemView::SingleSelection);
    }

signals:
    void clientDropped(const QString& clientId, const QString& newGroup);

protected:
    void dropEvent(QDropEvent* event) override {
        QList<QTreeWidgetItem*> before = selectedItems();
        QTreeWidget::dropEvent(event);
        QTreeWidgetItem* moved = nullptr;
        if (!before.isEmpty()) {
            moved = before.first();
        } else {
            QList<QTreeWidgetItem*> after = selectedItems();
            moved = after.isEmpty() ? nullptr : after.first();
        }
        if (!moved) {
            return;
        }
        if (moved->data(0, kRoleType).toInt() != kItemTypeClient) {
            return;
        }
        QString clientId = moved->data(0, kRoleClientId).toString();
        QString groupName = QStringLiteral("未分�?);
        if (QTreeWidgetItem* parentItem = moved->parent()) {
            groupName = parentItem->data(0, kRoleGroupName).toString();
        }
        emit clientDropped(clientId, groupName);
    }
};

class StreamTile : public QWidget {
    Q_OBJECT
public:
    static constexpr int kGridWidth = 400;
    static constexpr int kGridHeight = 225;
    static constexpr double kAspectRatio = static_cast<double>(kGridHeight) / static_cast<double>(kGridWidth);

    enum class StatusIndicator {
        None,
        Online,
        Offline,
        Attention
    };

    StreamTile(const QString& clientId, quint32 ssrc, QWidget* parent = nullptr)
        : QWidget(parent), clientId_(clientId), ssrc_(ssrc), displayName_(clientId) {
        applyGridSizing(true);
        setDragEnabled(true);
        indicator_ = StatusIndicator::Attention;
    }

    void setFrame(const QImage& frame) {
        bool firstFrame = !hasFrame_;
        currentFrame_ = frame;
        hasFrame_ = true;
        
        // 极限优化：只在图像尺寸变化时清除缓存，减少不必要的重缩放
        if (cachedScaledFrame_.isNull() || 
            (frame.width() != cachedScaledFrame_.width() && frame.height() != cachedScaledFrame_.height())) {
            cachedScaledFrame_ = QImage();
            cachedSize_ = QSize();
        }

        if (!frame.isNull() && frame.width() > 0) {
            double newRatio = static_cast<double>(frame.height()) / static_cast<double>(frame.width());
            newRatio = std::clamp(newRatio, 0.3, 3.0);
            if (std::abs(newRatio - aspectRatio_) > 0.01) {
                aspectRatio_ = newRatio;
                emit aspectRatioChanged(clientId_);
            } else if (firstFrame) {
                emit aspectRatioChanged(clientId_);
            }
        } else if (firstFrame) {
            emit aspectRatioChanged(clientId_);
        }

        // 极限优化：使用update()而非repaint()，让Qt优化重绘时机
        update();
    }

    void setStatusText(const QString& text) {
        statusText_ = text;
        update();
    }

    void setStats(double fps, double mbps) {
        fps_ = fps;
        mbps_ = mbps;
        update();
    }

    void setErrorMessage(const QString& text) {
        errorMessage_ = text;
        update();
    }

    void setDisplayName(const QString& name) {
        displayName_ = name;
        update();
    }

    QString clientId() const { return clientId_; }
    quint32 ssrc() const { return ssrc_; }
    QImage currentFrame() const { return currentFrame_; }
    void setDragEnabled(bool enabled) {
        dragEnabled_ = enabled;
        setAcceptDrops(enabled);
    }
    void applyGridSizing(bool gridMode, int widthOverride = 0) {
        if (gridMode) {
            int width = widthOverride > 0 ? widthOverride : kGridWidth;
            width = qMax(120, width);
            int height = static_cast<int>(std::round(width * aspectRatio_));
            setMinimumSize(width, height);
            setMaximumSize(width, height);
            setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        } else {
            setMinimumSize(0, 0);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        }
    }
    void setIndicator(StatusIndicator indicator) {
        indicator_ = indicator;
        update();
    }

signals:
    void contextMenuRequested(StreamTile* tile, const QPoint& globalPos);
    void tileDropped(const QString& targetId, const QString& sourceId);
    void tileDoubleClicked(const QString& clientId);
    void aspectRatioChanged(const QString& clientId);

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            pressPos_ = event->pos();
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!dragEnabled_ || !(event->buttons() & Qt::LeftButton)) {
            QWidget::mouseMoveEvent(event);
            return;
        }
        if ((event->pos() - pressPos_).manhattanLength() < QApplication::startDragDistance()) {
            QWidget::mouseMoveEvent(event);
            return;
        }
        QDrag* drag = new QDrag(this);
        auto* mimeData = new QMimeData();
        mimeData->setData("application/x-streamtile", clientId_.toUtf8());
        drag->setMimeData(mimeData);
        drag->exec(Qt::MoveAction);
        QWidget::mouseMoveEvent(event);
    }

    void enterEvent(QEnterEvent* event) override {
        hovered_ = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_ = false;
        update();
        QWidget::leaveEvent(event);
    }
    
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        // 窗口尺寸变化时清除缓存，强制重新缩放
        cachedScaledFrame_ = QImage();
        cachedSize_ = QSize();
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        if (!dragEnabled_) {
            event->ignore();
            return;
        }
        if (event->mimeData()->hasFormat("application/x-streamtile")) {
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
    }

    void dropEvent(QDropEvent* event) override {
        if (!dragEnabled_) {
            event->ignore();
            return;
        }
        if (!event->mimeData()->hasFormat("application/x-streamtile")) {
            event->ignore();
            return;
        }
        const QString sourceId = QString::fromUtf8(event->mimeData()->data("application/x-streamtile"));
        if (!sourceId.isEmpty() && sourceId != clientId_) {
            emit tileDropped(clientId_, sourceId);
        }
        event->acceptProposedAction();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            emit tileDoubleClicked(clientId_);
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);  // 启用抗锯齿提升清晰度
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);  // 启用平滑变换提升清晰�?
        painter.fillRect(rect(), kTileBackground);

        if (hasFrame_ && !currentFrame_.isNull()) {
            // 缓存缩放后的图像，避免每次paintEvent都重新缩�?            // 只在窗口尺寸变化或帧内容变化时重新缩放，大幅提升多客户端性能
            // 使用SmoothTransformation提升清晰度（替代FastTransformation�?            const QSize currentSize = this->size();
            // 计算当前帧的hash（使用图像尺寸和部分像素数据�?            qint64 currentFrameHash = qHash(QString::number(currentFrame_.width()) + QString::number(currentFrame_.height()));
            if (!currentFrame_.isNull() && currentFrame_.width() > 0 && currentFrame_.height() > 0) {
                // 使用图像左上角的一小块区域来计算hash，避免计算整个图�?                const int sampleSize = qMin(100, qMin(currentFrame_.width(), currentFrame_.height()));
                QImage sample = currentFrame_.copy(0, 0, sampleSize, sampleSize);
                currentFrameHash ^= qHash(sample.constBits(), sample.sizeInBytes());
            }
            if (cachedScaledFrame_.isNull() || cachedSize_ != currentSize || cachedFrameHash_ != currentFrameHash) {
                cachedFrameHash_ = currentFrameHash;
                cachedScaledFrame_ = currentFrame_.scaled(currentSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                cachedSize_ = currentSize;
            }
            const QPoint topLeft((width() - cachedScaledFrame_.width()) / 2, (height() - cachedScaledFrame_.height()) / 2);
            painter.drawImage(topLeft, cachedScaledFrame_);
        } else {
            painter.setPen(QColor(160, 160, 160));
            painter.drawText(rect(), Qt::AlignCenter, statusText_.isEmpty() ? tr("等待视频�?..") : statusText_);
        }

        painter.setPen(QPen(QColor(0, 0, 0, 160), 3));
        painter.drawRect(rect().adjusted(1, 1, -2, -2));

        if (hovered_) {
            painter.setPen(QPen(QColor(70, 160, 255, dragEnabled_ ? 200 : 120), 2));
            painter.drawRect(rect().adjusted(2, 2, -3, -3));
        }

        const int footerHeight = 18;
        QRect footerRect(0, height() - footerHeight, width(), footerHeight);
        QLinearGradient gradient(footerRect.topLeft(), footerRect.bottomLeft());
        gradient.setColorAt(0.0, QColor(10, 10, 10, 230));
        gradient.setColorAt(1.0, QColor(25, 25, 25, 230));
        painter.fillRect(footerRect, gradient);

        QColor dotColor(128, 128, 128);
        switch (indicator_) {
        case StatusIndicator::Online:
            dotColor = QColor(0, 200, 0);
            break;
        case StatusIndicator::Offline:
            dotColor = QColor(220, 30, 30);
            break;
        case StatusIndicator::Attention:
            dotColor = QColor(240, 180, 0);
            break;
        case StatusIndicator::None:
        default:
            dotColor = QColor(128, 128, 128);
            break;
        }
        const int dotRadius = 6;
        QPoint dotCenter(footerRect.left() + 12, footerRect.center().y());
        painter.setBrush(dotColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(dotCenter, dotRadius, dotRadius);

        QString combined = displayName_.isEmpty() ? clientId_ : displayName_;

        QString statsText;
        if (fps_ > 0.0 || mbps_ > 0.0) {
            statsText = QStringLiteral("%1 fps | %2 Mbps")
                            .arg(QString::number(fps_, 'f', fps_ >= 10.0 ? 1 : 2),
                                 QString::number(mbps_, 'f', mbps_ >= 10.0 ? 2 : 3));
        } else {
            statsText = tr("暂无数据");
        }

        QStringList footerSegments;
        footerSegments << combined;
        if (!statsText.isEmpty()) {
            footerSegments << statsText;
        }
        const QString footerText = footerSegments.join(QStringLiteral(" | "));

        QFont footerFont = painter.font();
        footerFont.setPointSizeF(footerFont.pointSizeF() - 1.0);
        footerFont.setBold(false);
        painter.setFont(footerFont);
        painter.setPen(QColor(200, 200, 200));
        painter.drawText(footerRect.adjusted(26, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, footerText);

        if (!errorMessage_.isEmpty()) {
            painter.setPen(QColor(255, 180, 0));
            QFont errorFont = painter.font();
            errorFont.setBold(true);
            painter.setFont(errorFont);
            QString warn = QStringLiteral("⚠ %1").arg(errorMessage_);
            painter.drawText(footerRect.adjusted(0, 0, -12, 0), Qt::AlignRight | Qt::AlignVCenter, warn);
        }
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        emit contextMenuRequested(this, event->globalPos());
    }

private:
    QString clientId_;
    quint32 ssrc_{0};
    QImage currentFrame_;
    QImage cachedScaledFrame_;  // 缓存缩放后的图像，提升多客户端性能
    QSize cachedSize_;  // 缓存时的窗口尺寸
    qint64 cachedFrameHash_{0};  // 缓存帧的hash，用于检测帧内容变化
    QString statusText_;
    bool hasFrame_{false};
    bool dragEnabled_{true};
    bool hovered_{false};
    QPoint pressPos_;
    StatusIndicator indicator_{StatusIndicator::None};
    QString displayName_;
    double fps_{0.0};
    double mbps_{0.0};
    QString errorMessage_;
    double aspectRatio_{kAspectRatio};
};

using StatusIndicator = StreamTile::StatusIndicator;

class FullscreenView : public QWidget {
    Q_OBJECT
public:
    explicit FullscreenView(const QString& clientId, QWidget* parent = nullptr)
        : QWidget(parent), clientId_(clientId) {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setStyleSheet(QStringLiteral("background-color: black; color: white;"));

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);  // 去除边距，铺满窗�?        layout->setSpacing(0);

        statsLabel_ = new QLabel(this);
        statsLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        statsLabel_->setStyleSheet(QStringLiteral("color: #cccccc; background-color: rgba(0,0,0,128); padding: 4px 8px;"));
        statsLabel_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        layout->addWidget(statsLabel_, 0, Qt::AlignTop);

        imageLabel_ = new QLabel(this);
        imageLabel_->setAlignment(Qt::AlignCenter);
        imageLabel_->setScaledContents(false);  // 不使�?scaledContents，使用自定义缩放
        layout->addWidget(imageLabel_, 1);
    }

    QString clientId() const { return clientId_; }

public slots:
    void setFrame(const QImage& frame) {
        lastFrame_ = frame;
        updatePixmap();
    }

    void setStatsText(const QString& text) {
        statsLabel_->setText(text);
    }

signals:
    void viewerClosed(const QString& clientId);
    void exitRequested(const QString& clientId);

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            event->accept();
            emit exitRequested(clientId_);
            close();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        Q_UNUSED(event);
        emit exitRequested(clientId_);
        close();
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        updatePixmap();
    }

    void closeEvent(QCloseEvent* event) override {
        emit viewerClosed(clientId_);
        QWidget::closeEvent(event);
    }

private:
    void updatePixmap() {
        if (lastFrame_.isNull()) {
            imageLabel_->clear();
            return;
        }
        const QSize targetSize = imageLabel_->size();
        if (targetSize.isEmpty()) {
            return;
        }
        
        // 优化：缓存缩放后的图像，避免每次resize都重新缩�?        // 使用简单的静态变量缓存，如果尺寸变化或帧内容变化则重新缩�?        static QSize cachedTargetSize;
        static QPixmap cachedPixmap;
        static qint64 cachedFrameHash = 0;
        
        // 计算当前帧的hash（使用图像尺寸和部分像素数据�?        qint64 currentFrameHash = qHash(QString::number(lastFrame_.width()) + QString::number(lastFrame_.height()));
        if (!lastFrame_.isNull() && lastFrame_.width() > 0 && lastFrame_.height() > 0) {
            // 使用图像左上角的一小块区域来计算hash，避免计算整个图�?            const int sampleSize = qMin(100, qMin(lastFrame_.width(), lastFrame_.height()));
            QImage sample = lastFrame_.copy(0, 0, sampleSize, sampleSize);
            currentFrameHash ^= qHash(sample.constBits(), sample.sizeInBytes());
        }
        
        // 如果尺寸或帧内容变化，重新缩�?        if (cachedTargetSize != targetSize || cachedFrameHash != currentFrameHash || cachedPixmap.isNull()) {
            cachedTargetSize = targetSize;
            cachedFrameHash = currentFrameHash;
        // 使用 IgnoreAspectRatio 铺满窗口，避免左右黑�?            cachedPixmap = QPixmap::fromImage(lastFrame_).scaled(targetSize,
                                                                    Qt::IgnoreAspectRatio,
                                                                  Qt::SmoothTransformation);
        }
        
        imageLabel_->setPixmap(cachedPixmap);
    }

    QString clientId_;
    QLabel* statsLabel_{nullptr};
    QLabel* imageLabel_{nullptr};
    QImage lastFrame_;
};

// 以下三个类已废弃, 由 JpegReceiver 统一处理:
// - JpegReassembler: 被 JpegReceiver 内部重组逻辑替代
// - VideoRecorder: 录制功能待重新设计  
// - StreamPlayer: 被 JpegReceiver 替代

class PlaceholderTile : public QWidget {
public:
    explicit PlaceholderTile(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setTileSize(int width, int height) {
        setMinimumSize(width, height);
        setMaximumSize(width, height);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor(12, 12, 12, 180));
        painter.setPen(QPen(QColor(60, 60, 60), 1, Qt::DashLine));
        painter.drawRect(rect().adjusted(1, 1, -2, -2));
        painter.setPen(QColor(90, 90, 90));
        QFont labelFont = painter.font();
        labelFont.setPointSizeF(labelFont.pointSizeF() - 1.0);
        painter.setFont(labelFont);
        painter.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignCenter, QObject::tr("空槽"));
    }
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      config_(core::AppConfig::FromDefaults()),
      restApiManager_(new QNetworkAccessManager(this)) {
    const QString configPath =
        QCoreApplication::applicationDirPath() + QStringLiteral("/config/app.json");
    config_ = core::AppConfig::FromFile(configPath);
    qInfo() << "Console configuration source:" << config_.source();

    setupUi();
    ensureQtRuntimeArtifacts();
    initializeServices();
    
    // 初始化集成的 CommandController 功能 (纯UDP架构)
    initDatabase();
    
    // 初始�?UDP 接收�?(替代 CommandController)
    udpReceiver_ = new QUdpSocket(this);
    if (udpReceiver_->bind(QHostAddress::Any, 10000, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        connect(udpReceiver_, &QUdpSocket::readyRead, this, &MainWindow::handleUdpDatagram);
        qInfo() << "[Console] UDP receiver listening on port 10000 (integrated CommandController)";
    } else {
        qWarning() << "[Console] Failed to bind UDP receiver on port 10000:" << udpReceiver_->errorString();
    }
    
    // 心跳超时检查定时器 (60秒检查一�?
    heartbeatCheckTimer_ = new QTimer(this);
    heartbeatCheckTimer_->setInterval(60000);
    connect(heartbeatCheckTimer_, &QTimer::timeout, this, &MainWindow::checkClientHeartbeats);
    heartbeatCheckTimer_->start();
    
    // 初始化视频接收器 (UDP 5004)
    videoReceiver_ = new JpegReceiver(5004, this);
    bool connected = connect(videoReceiver_, &JpegReceiver::frameReceived, this, &MainWindow::handleVideoFrame);
    qInfo() << "[Console] Video signal connection:" << (connected ? "SUCCESS" : "FAILED");
    connect(videoReceiver_, &JpegReceiver::error, this, [](const QString& err) {
        qWarning() << "[Console] Video receiver error:" << err;
    });
    if (videoReceiver_->start()) {
        qInfo() << "[Console] Video receiver started on UDP port 5004";
    } else {
        qWarning() << "[Console] Failed to start video receiver";
    }
    
    // 加载敏感�?    sensitiveWords_ = loadSensitiveWords();
    qInfo() << "[Console] Loaded" << sensitiveWords_.size() << "sensitive words";
    
    setupControlChannel();
    
    connect(restApiManager_, &QNetworkAccessManager::finished,
            this, &MainWindow::handleRestApiReply);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    shuttingDown_ = true;
    stopServices();
    // Wait a bit for processes to terminate
    QThread::msleep(500);
    event->accept();
}

MainWindow::~MainWindow() {
    shuttingDown_ = true;
    stopServices();
    for (auto it = activePlayers_.begin(); it != activePlayers_.end(); ++it) {
        StreamPlayer* player = it.value();
        if (player) {
            if (controlChannel_) {
                sendUnsubscribe(it.key(), player->ssrc(), player->localPort());
            }
            player->stop();
            delete player;
        }
    }
    activePlayers_.clear();
    qDeleteAll(activeTiles_);
    activeTiles_.clear();
    qDeleteAll(placeholderTiles_);
    placeholderTiles_.clear();
}

void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("Desktop Console"));
    resize(1400, 900);

    auto* toolBar = addToolBar(QStringLiteral("Main"));
    toolBar->setMovable(false);
    toolBar->setObjectName(QStringLiteral("main_toolbar"));
    toolBar->setVisible(false);  // 隐藏工具栏，只使用右键菜�?
    statusLabel_ = new QLabel(QStringLiteral("Disconnected"), this);
    statusLabel_->setStyleSheet(QStringLiteral("color: white;"));
    statusBar()->setStyleSheet(QStringLiteral("QStatusBar { background-color: #202020; color: white; }"));
    statusBar()->addPermanentWidget(statusLabel_);

    metricsLabel_ = new QLabel(this);
    metricsLabel_->setStyleSheet(QStringLiteral("color: white;"));
    statusBar()->addPermanentWidget(metricsLabel_);

    errorLabel_ = new QLabel(this);
    errorLabel_->setStyleSheet(QStringLiteral("color: #ff8080;"));
    statusBar()->addPermanentWidget(errorLabel_);

    clientTree_ = new ClientTreeWidget(this);
    clientTree_->setColumnCount(1);
    clientTree_->setFixedWidth(220);
    clientTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    clientTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(clientTree_, &QTreeWidget::itemActivated, this, &MainWindow::handleClientItemActivated);
    connect(clientTree_, &ClientTreeWidget::customContextMenuRequested, this, &MainWindow::handleClientContextMenu);
    connect(clientTree_, &ClientTreeWidget::clientDropped, this, &MainWindow::handleClientDropped);


    previewContainer_ = new QWidget(this);
    previewContainer_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(previewContainer_, &QWidget::customContextMenuRequested,
            this, &MainWindow::handlePreviewContextMenu);
    previewContainer_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    previewLayout_ = new QGridLayout(previewContainer_);
    previewLayout_->setContentsMargins(8, 8, 8, 8);
    previewLayout_->setSpacing(8);
    previewLayout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    previewMarginsNormal_ = previewLayout_->contentsMargins();
    previewSpacingNormal_ = previewLayout_->spacing();

    previewScrollArea_ = new QScrollArea(this);
    previewScrollArea_->setWidget(previewContainer_);
    previewScrollArea_->setWidgetResizable(true);
    previewScrollArea_->setFrameShape(QFrame::NoFrame);
    previewScrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewScrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewScrollArea_->viewport()->installEventFilter(this);

    wallHeader_ = new QWidget(this);
    wallHeader_->setObjectName(QStringLiteral("wallHeader"));
    wallHeader_->setStyleSheet(QStringLiteral(
        "#wallHeader { background-color: #111827; border-bottom: 1px solid rgba(75,85,99,0.4); }"));
    auto* headerLayout = new QHBoxLayout(wallHeader_);
    headerLayout->setContentsMargins(12, 8, 12, 8);
    headerLayout->setSpacing(12);

    wallStatsLabel_ = new QLabel(tr("监控"), wallHeader_);
    wallStatsLabel_->setStyleSheet(QStringLiteral("color: #d1d5db; font-weight: 600;"));
    headerLayout->addWidget(wallStatsLabel_);
    headerLayout->addStretch();
    
    // 保存时间设置按钮
    saveTimeButton_ = new QPushButton(tr("💾 保存时间"), wallHeader_);
    saveTimeButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #1f2937; color: #e5e7eb; border: 1px solid #374151; padding: 4px 12px; }"
        "QPushButton:hover { background-color: #374151; }"
        "QPushButton:pressed { background-color: #4b5563; }"));
    saveTimeButton_->setCursor(Qt::PointingHandCursor);
    headerLayout->addWidget(saveTimeButton_);
    connect(saveTimeButton_, &QPushButton::clicked, this, &MainWindow::handleSaveTimeSettings);

    groupFilterCombo_ = new QComboBox(wallHeader_);
    groupFilterCombo_->setMinimumWidth(160);
    groupFilterCombo_->setStyleSheet(QStringLiteral(
        "QComboBox { background-color: #1f2937; color: #e5e7eb; border: 1px solid #374151; padding: 2px 8px; }"
        "QComboBox::drop-down { border: none; }"));
    headerLayout->addWidget(groupFilterCombo_);
    connect(groupFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::handleGroupFilterChanged);

    auto* previewPanel = new QWidget(this);
    auto* previewPanelLayout = new QVBoxLayout(previewPanel);
    previewPanelLayout->setContentsMargins(0, 0, 0, 0);
    previewPanelLayout->setSpacing(0);
    previewPanelLayout->addWidget(wallHeader_, 0);
    previewPanelLayout->addWidget(previewScrollArea_, 1);

    auto* splitter = new QSplitter(this);
    splitter->addWidget(clientTree_);
    splitter->addWidget(previewPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);

    setCentralWidget(splitter);

    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0, 0, 0));
    pal.setColor(QPalette::WindowText, QColor(220, 220, 220));
    setPalette(pal);

    clientTree_->setStyleSheet(QStringLiteral(
        "QTreeWidget { background-color: #101010; color: #f0f0f0; }"
        "QTreeWidget::item:selected { background-color: #303030; color: #ffffff; }"));
    previewContainer_->setStyleSheet(QStringLiteral("background-color: #000000;"));

    metadataPath_ = QCoreApplication::applicationDirPath() + QStringLiteral("/config/client_metadata.json");
    loadClientMetadata();

    rebuildToolbar();
    populateGroupFilterOptions();
    applyLayoutPreset(currentLayoutPreset_);
    
    // 设置状态栏更新定时器（�?00ms更新一次，避免频繁更新�?    statusBarUpdateTimer_ = new QTimer(this);
    statusBarUpdateTimer_->setInterval(500);
    statusBarUpdateTimer_->setSingleShot(false);
    connect(statusBarUpdateTimer_, &QTimer::timeout, this, &MainWindow::updateStatusBarStats);
    statusBarUpdateTimer_->start();
    
    // 设置客户端树重建定时器（批量更新，避免频繁重建）
    clientTreeRebuildTimer_ = new QTimer(this);
    clientTreeRebuildTimer_->setInterval(200);  // 200ms 批量更新
    clientTreeRebuildTimer_->setSingleShot(true);
    connect(clientTreeRebuildTimer_, &QTimer::timeout, this, [this]() {
        if (clientTreeNeedsRebuild_) {
            clientTreeNeedsRebuild_ = false;
            rebuildClientTree();
        }
    });
    
    updateStatusBarStats();  // 立即更新一�?    updateWallHeaderStats();
}

static bool isPortAvailable(quint16 port) {
    if (port == 0) {
        return true;
    }
    QTcpServer probe;
    probe.setMaxPendingConnections(1);
    const bool ok = probe.listen(QHostAddress::Any, port);
    if (ok) {
        probe.close();
    }
    return ok;
}

static void tryTerminateProcessByName(const QString& executableName) {
#if defined(Q_OS_WIN)
    if (executableName.isEmpty()) {
        return;
    }
    QString fileName = QFileInfo(executableName).fileName();
    if (fileName.isEmpty()) {
        return;
    }
    QStringList args{QStringLiteral("/F"), QStringLiteral("/IM"), fileName};
    QProcess killProcess;
    killProcess.setProgram(QStringLiteral("taskkill"));
    killProcess.setArguments(args);
    killProcess.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    killProcess.start();
    killProcess.waitForFinished(2000);
#else
    Q_UNUSED(executableName);
#endif
}

void MainWindow::initializeServices() {
    // Ensure Qt runtime artifacts (including SQL drivers) are available
    ensureQtRuntimeArtifacts();
    
    const QString baseDir = QCoreApplication::applicationDirPath();
    struct ServiceDef {
        QString name;
        QString executable;
        QStringList arguments;
        bool autoRestart{true};
        quint16 listenPort{0};
    };
    QVector<ServiceDef> defs;
    // 纯UDP架构：所有服务都已集成到 DesktopConsole
    // - CommandController 功能已集成（UDP 接收�?+ 数据库）
    // - MonitorServer（WebSocket 8765）不再需�?    // - StreamServer 也不再需要（视频流和控制命令都通过 UDP 直连�?    // defs.append(ServiceDef{QStringLiteral("MonitorServer"),
    //                        QStringLiteral("MonitorServer.exe"),
    //                        {},
    //                        true,
    //                        8765});
    // defs.append(ServiceDef{QStringLiteral("StreamServer"),
    //                        QStringLiteral("StreamServer.exe"),
    //                        {},
    //                        true,
    //                        7000});
    // defs.append(ServiceDef{QStringLiteral("CommandController"),
    //                        QStringLiteral("CommandController.exe"),
    //                        {},
    //                        true,
    //                        8080});

#if defined(Q_OS_WIN)
    for (const ServiceDef& def : defs) {
        const QString exePath = QDir(baseDir).filePath(def.executable);
        const QString exeName = QFileInfo(exePath).fileName();
        terminateProcessIfRunning(exeName);
    }
#endif

    services_.clear();
    services_.reserve(defs.size());
    for (const ServiceDef& def : defs) {
        ManagedService svc;
        svc.name = def.name;
        svc.executable = QDir(baseDir).filePath(def.executable);
        svc.arguments = def.arguments;
        svc.autoRestart = def.autoRestart;
        services_.append(std::move(svc));
    }

    for (int i = 0; i < services_.size(); ++i) {
        startService(i);
    }
}

bool MainWindow::ensureDirectory(const QString& path) {
    QDir dir(path);
    if (dir.exists()) {
        return true;
    }
    return dir.mkpath(QStringLiteral("."));
}

bool MainWindow::ensureFileCopied(const QString& source, const QString& destination) {
    QFileInfo destInfo(destination);
    ensureDirectory(destInfo.path());

    if (QFile::exists(destination)) {
        return true;
    }
    QFile srcFile(source);
    if (!srcFile.exists()) {
        qWarning() << "[Console] Required Qt runtime not found at" << source;
        return false;
    }
    if (!QFile::copy(source, destination)) {
        QFile dst(destination);
        qWarning() << "[Console] Failed to copy" << source << "to" << destination
                   << "-" << dst.errorString();
        return false;
    }
    return true;
}

QString MainWindow::detectDllDirectory() const {
#if defined(Q_OS_WIN)
    QStringList candidateDirs;

    const QString libInfoPath = QLibraryInfo::path(QLibraryInfo::BinariesPath);
    if (!libInfoPath.isEmpty()) {
        candidateDirs << libInfoPath;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QStringList envKeys = {QStringLiteral("QT_BIN_DIR"), QStringLiteral("QT_DIR"), QStringLiteral("QT_HOME")};
    for (const QString& key : envKeys) {
        const QString value = env.value(key);
        if (!value.isEmpty()) {
            candidateDirs << value;
            candidateDirs << QDir(value).filePath(QStringLiteral("bin"));
        }
    }

    QString found = QStandardPaths::findExecutable(QStringLiteral("Qt6Sql.dll"));
    if (!found.isEmpty()) {
        candidateDirs << QFileInfo(found).absolutePath();
    }

    const QStringList pathEntries =
        env.value(QStringLiteral("PATH")).split(QDir::listSeparator(), Qt::SkipEmptyParts);
    candidateDirs.append(pathEntries);

    candidateDirs << QStringLiteral("C:/Qt/6.10.0/mingw_64/bin");
    candidateDirs << QStringLiteral("C:/Qt/6.5.3/mingw_64/bin");

    for (const QString& dir : candidateDirs) {
        if (dir.isEmpty()) {
            continue;
        }
        const QString candidate = QDir(dir).filePath(QStringLiteral("Qt6Sql.dll"));
        if (QFile::exists(candidate)) {
            return QFileInfo(candidate).absolutePath();
        }
    }
#endif
    return QString();
}

void MainWindow::ensureQtRuntimeArtifacts() {
#if defined(Q_OS_WIN)
    const QString targetDir = QCoreApplication::applicationDirPath();
    const QString qtBinPath = detectDllDirectory();
    QString pluginPath = QLibraryInfo::path(QLibraryInfo::PluginsPath);

    if (!qtBinPath.isEmpty()) {
        const QStringList requiredDlls = {
            QStringLiteral("Qt6Sql.dll"),
            QStringLiteral("Qt6Network.dll"),
            QStringLiteral("Qt6Core.dll"),
            QStringLiteral("Qt6Gui.dll"),
            QStringLiteral("Qt6Widgets.dll"),
        };

        for (const QString& dll : requiredDlls) {
            const QString src = QDir(qtBinPath).filePath(dll);
            const QString dst = QDir(targetDir).filePath(dll);
            ensureFileCopied(src, dst);
        }
    } else {
        qWarning() << "[Console] Failed to detect Qt bin directory; Qt DLLs may be missing.";
    }

    QStringList pluginSearchRoots;
    auto appendPluginRoot = [&pluginSearchRoots](const QString& root) {
        if (root.isEmpty()) {
            return;
        }
        const QString normalized = QDir(root).absolutePath();
        if (!pluginSearchRoots.contains(normalized)) {
            pluginSearchRoots.append(normalized);
        }
    };

    appendPluginRoot(pluginPath);

    if (!qtBinPath.isEmpty()) {
        appendPluginRoot(QDir(qtBinPath).filePath(QStringLiteral("../plugins")));
        appendPluginRoot(QDir(qtBinPath).filePath(QStringLiteral("../plugins/sqldrivers")));
    }

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString pluginEnv = env.value(QStringLiteral("QT_PLUGIN_PATH"));
    for (const QString& entry :
         pluginEnv.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
        appendPluginRoot(entry);
    }
    appendPluginRoot(env.value(QStringLiteral("QT_PLUGIN_DIR")));

    appendPluginRoot(QStringLiteral("C:/Qt/6.10.0/mingw_64/plugins"));
    appendPluginRoot(QStringLiteral("C:/Qt/6.5.3/mingw_64/plugins"));

    const QString sqlitePluginName = QStringLiteral("qsqlite.dll");
    QString pluginSrc;
    for (const QString& root : std::as_const(pluginSearchRoots)) {
        const QString candidate = QFileInfo(root).isDir()
                                       ? QDir(root).filePath(QStringLiteral("sqldrivers/%1").arg(sqlitePluginName))
                                       : QString();
        if (!candidate.isEmpty() && QFile::exists(candidate)) {
            pluginSrc = candidate;
            break;
        }
        if (QFileInfo(root).isFile() && QFileInfo(root).fileName().compare(sqlitePluginName, Qt::CaseInsensitive) ==
                                         0) {
            pluginSrc = QFileInfo(root).absoluteFilePath();
            break;
        }
    }

    if (!pluginSrc.isEmpty()) {
        const QString pluginDstDir = QDir(targetDir).filePath(QStringLiteral("sqldrivers"));
        ensureDirectory(pluginDstDir);
        const QString pluginDst = QDir(pluginDstDir).filePath(sqlitePluginName);
        ensureFileCopied(pluginSrc, pluginDst);
    } else {
        qWarning() << "[Console] SQLite driver not found in plugin search roots:" << pluginSearchRoots;
    }
#endif
}

void MainWindow::startService(int index) {
    if (index < 0 || index >= services_.size()) {
        return;
    }
    ManagedService& svc = services_[index];
    if (svc.process && svc.process->state() != QProcess::NotRunning) {
        return;
    }
    if (svc.process) {
        svc.process->deleteLater();
        svc.process = nullptr;
    }

    QFileInfo info(svc.executable);
    if (!info.exists()) {
        const QString fallback = QDir::current().filePath(info.fileName());
        QFileInfo fallbackInfo(fallback);
        if (fallbackInfo.exists()) {
            info = fallbackInfo;
            svc.executable = fallbackInfo.absoluteFilePath();
        } else {
            qWarning() << "[Console] Service executable not found:" << svc.executable;
            svc.autoRestart = false;
            return;
        }
    }

    if (svc.listenPort != 0 && !isPortAvailable(svc.listenPort)) {
        qWarning() << "[Console] Port" << svc.listenPort << "is already in use; attempting to terminate stale"
                   << svc.name;
        tryTerminateProcessByName(info.fileName());
        QThread::msleep(150);
        if (!isPortAvailable(svc.listenPort)) {
            qWarning() << "[Console] Port" << svc.listenPort << "still in use. Skip starting" << svc.name;
            if (!shuttingDown_) {
                const int delay = qMin(30000, 2000 * qMax(1, svc.restartAttempts + 1));
                svc.restartAttempts++;
                QTimer::singleShot(delay, this, [this, index]() { startService(index); });
            }
            return;
        }
    }

    QProcess* process = new QProcess(this);
    svc.process = process;
    svc.pendingOutput.clear();
    svc.restartAttempts = 0;

    process->setProgram(svc.executable);
    process->setArguments(svc.arguments);
    process->setWorkingDirectory(info.absolutePath());
    // Use separate channels to capture both stdout and stderr
    process->setProcessChannelMode(QProcess::SeparateChannels);
    // Don't hide window for CommandController to see its output
    // But still capture output via QProcess
    if (svc.name != QStringLiteral("CommandController")) {
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
#if defined(Q_OS_WIN)
        args->flags |= CREATE_NO_WINDOW;
#endif
    });
    }

    // Capture both stdout and stderr separately
    connect(process, &QProcess::readyReadStandardOutput, this, [this, index]() {
        if (index < 0 || index >= services_.size()) {
            return;
        }
        ManagedService& svc = services_[index];
        if (!svc.process) {
            return;
        }
        QByteArray data = svc.process->readAllStandardOutput();
        svc.pendingOutput.append(data);
        int newline = 0;
        while ((newline = svc.pendingOutput.indexOf('\n')) >= 0) {
            QByteArray line = svc.pendingOutput.left(newline);
            svc.pendingOutput.remove(0, newline + 1);
            const QString text = QString::fromLocal8Bit(line.trimmed());
            if (!text.isEmpty()) {
                qInfo().noquote() << QString("[%1] %2").arg(svc.name, text);
            }
        }
    });
    
    connect(process, &QProcess::readyReadStandardError, this, [this, index]() {
        if (index < 0 || index >= services_.size()) {
            return;
        }
        ManagedService& svc = services_[index];
        if (!svc.process) {
            return;
        }
        QByteArray data = svc.process->readAllStandardError();
        svc.pendingOutput.append(data);
        int newline = 0;
        while ((newline = svc.pendingOutput.indexOf('\n')) >= 0) {
            QByteArray line = svc.pendingOutput.left(newline);
            svc.pendingOutput.remove(0, newline + 1);
            const QString text = QString::fromLocal8Bit(line.trimmed());
            if (!text.isEmpty()) {
                qInfo().noquote() << QString("[%1] %2").arg(svc.name, text);
            }
        }
    });

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this, index](int exitCode, QProcess::ExitStatus status) {
                handleServiceFinished(index, exitCode, status);
            });

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString qtBinPath = QLibraryInfo::path(QLibraryInfo::BinariesPath);
    const QString pluginPath = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    const QString appDir = QCoreApplication::applicationDirPath();

#if defined(Q_OS_WIN)
    const QChar pathSep = QLatin1Char(';');
#else
    const QChar pathSep = QLatin1Char(':');
#endif

    auto ensurePathContains = [&](const QString& value, const QString& entry) -> QString {
        if (value.isEmpty()) {
            return entry;
        }
        const QStringList parts = value.split(pathSep, Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            if (QDir::toNativeSeparators(part).compare(QDir::toNativeSeparators(entry), Qt::CaseInsensitive) == 0) {
                return value;
            }
        }
        return entry + pathSep + value;
    };

    if (!qtBinPath.isEmpty()) {
        env.insert(QStringLiteral("PATH"), ensurePathContains(env.value(QStringLiteral("PATH")), qtBinPath));
    }
    if (!pluginPath.isEmpty()) {
        env.insert(QStringLiteral("QT_PLUGIN_PATH"),
                   ensurePathContains(env.value(QStringLiteral("QT_PLUGIN_PATH")), pluginPath));
    }
    
    // Add application directory plugin paths for SQL drivers
    const QString appPluginsPath = QDir(appDir).filePath(QStringLiteral("plugins"));
    const QString appSqldriversPath = QDir(appDir).filePath(QStringLiteral("sqldrivers"));
    QStringList pluginPaths;
    if (QDir(appPluginsPath).exists()) {
        pluginPaths << appPluginsPath;
    }
    if (QDir(appSqldriversPath).exists()) {
        pluginPaths << appSqldriversPath;
    }
    if (!pluginPaths.isEmpty()) {
        QString currentPluginPath = env.value(QStringLiteral("QT_PLUGIN_PATH"));
        for (const QString& path : pluginPaths) {
            currentPluginPath = ensurePathContains(currentPluginPath, path);
        }
        env.insert(QStringLiteral("QT_PLUGIN_PATH"), currentPluginPath);
        qInfo() << "[Console] Set QT_PLUGIN_PATH for service:" << currentPluginPath;
    }
    
    process->setProcessEnvironment(env);

    process->start();
    if (!process->waitForStarted(5000)) {
        const QString err = process->errorString();
        qWarning() << "[Console] Failed to start service" << svc.name << svc.executable
                   << "-" << err;
        process->deleteLater();
        svc.process = nullptr;
        if (!shuttingDown_ && svc.autoRestart) {
            const int delay = qMin(30000, 2000 * qMax(1, svc.restartAttempts + 1));
            svc.restartAttempts++;
            QTimer::singleShot(delay, this, [this, index]() { startService(index); });
        }
        return;
    }

    qInfo() << "[Console] Started service" << svc.name << "pid" << process->processId();
}

void MainWindow::handleServiceFinished(int index, int exitCode, QProcess::ExitStatus status) {
    if (index < 0 || index >= services_.size()) {
        return;
    }
    ManagedService& svc = services_[index];
    if (svc.process) {
        svc.pendingOutput.append(svc.process->readAll());
        svc.process->deleteLater();
        svc.process = nullptr;
    }
    if (!svc.pendingOutput.isEmpty()) {
        const QString text = QString::fromLocal8Bit(svc.pendingOutput.trimmed());
        if (!text.isEmpty()) {
            qInfo().noquote() << QString("[%1] %2").arg(svc.name, text);
        }
        svc.pendingOutput.clear();
    }

    const QString statusText =
        (status == QProcess::CrashExit) ? QStringLiteral("crashed") : QStringLiteral("finished");
    qWarning() << "[Console] Service" << svc.name << statusText << "exitCode" << exitCode;

    if (shuttingDown_ || !svc.autoRestart || svc.requestedStop) {
        svc.requestedStop = false;
        return;
    }
    const int delay = qMin(15000, 2000 * qMax(1, svc.restartAttempts + 1));
    svc.restartAttempts++;
    QTimer::singleShot(delay, this, [this, index]() { startService(index); });
}

void MainWindow::stopServices() {
    qInfo() << "[Console] Stopping all services...";
    for (ManagedService& svc : services_) {
        svc.autoRestart = false;
        svc.requestedStop = true;
        if (!svc.process) {
            // Try to terminate by process name as fallback
            QFileInfo info(svc.executable);
            if (info.exists()) {
                const QString exeName = info.fileName();
                qInfo() << "[Console] Attempting to terminate" << exeName << "by name";
                tryTerminateProcessByName(exeName);
            }
            continue;
        }
        QProcess* process = svc.process;
        if (process->state() != QProcess::NotRunning) {
            qInfo() << "[Console] Terminating service" << svc.name << "pid" << process->processId();
            process->terminate();
            if (!process->waitForFinished(2000)) {
                qWarning() << "[Console] Service" << svc.name << "did not terminate, killing...";
                process->kill();
                if (!process->waitForFinished(1000)) {
                    qWarning() << "[Console] Failed to kill service" << svc.name << ", trying by name";
                    QFileInfo info(svc.executable);
                    if (info.exists()) {
                        tryTerminateProcessByName(info.fileName());
                    }
                }
            } else {
                qInfo() << "[Console] Service" << svc.name << "terminated successfully";
            }
        }
        process->deleteLater();
        svc.process = nullptr;
        svc.pendingOutput.clear();
    }
    qInfo() << "[Console] All services stopped";
}

void MainWindow::setupControlChannel() {
    // 完全直连方案：DesktopConsole作为客户端连接到StreamClient的WebSocket服务�?    // 不需要启动自己的WebSocket服务器（避免端口冲突�?    // ConsoleControlServer和ConsoleBroadcaster已禁�?    
    // 完全直连方案：启动UDP客户端发�?    clientDiscovery_ = std::make_unique<ClientDiscovery>(this);
    // 优化：使用防抖机制，避免频繁刷新客户端列表（增加�?秒）
    QTimer* clientListRefreshTimer = new QTimer(this);
    clientListRefreshTimer->setSingleShot(true);
    clientListRefreshTimer->setInterval(2000);  // 2秒防抖，进一步减少刷新频�?    connect(clientListRefreshTimer, &QTimer::timeout, this, &MainWindow::requestClientList);
    
    connect(clientDiscovery_.get(), &ClientDiscovery::clientDiscovered, this, [this, clientListRefreshTimer](const DiscoveredClient& client) {
        qInfo() << "[Console] Client discovered:" << client.clientId << "at" << client.ip;
        clientListRefreshTimer->start();  // 防抖刷新
        // 如果客户端正在监控但WebSocket未连接，尝试自动连接
        if (activePlayers_.contains(client.clientId)) {
            auto itChannel = directControlChannels_.find(client.clientId);
            if (itChannel == directControlChannels_.end() || !itChannel.value() || !itChannel.value()->isConnected()) {
                qInfo() << "[Console] Client" << client.clientId << "discovered and is being monitored, attempting to connect WebSocket";
                auto itPlayer = activePlayers_.find(client.clientId);
                if (itPlayer != activePlayers_.end()) {
                    StreamPlayer* player = itPlayer.value();
                    // 清理旧连接（如果存在�?                    if (itChannel != directControlChannels_.end() && itChannel.value()) {
                        itChannel.value()->deleteLater();
                        directControlChannels_.remove(client.clientId);
                    }
                    // 建立新连�?                    network::WsChannel* directChannel = new network::WsChannel(this);
                    directControlChannels_[client.clientId] = directChannel;
                    
                    connect(directChannel, &network::WsChannel::connected, this, [this, clientId = client.clientId, ssrc = player->ssrc(), port = player->localPort()]() {
                        qInfo() << "[Console] Auto-connected WebSocket to" << clientId;
                        sendDirectSubscribe(clientId, ssrc, port);
                    });
                    
                    connect(directChannel, &network::WsChannel::disconnected, this, [this, clientId = client.clientId]() {
                        qWarning() << "[Console] Direct WebSocket disconnected from" << clientId;
                        auto it = directControlChannels_.find(clientId);
                        if (it != directControlChannels_.end()) {
                            it.value()->deleteLater();
                            directControlChannels_.erase(it);
                        }
                        if (activePlayers_.contains(clientId)) {
                            qInfo() << "[Console] Client" << clientId << "is still being monitored, will attempt reconnect on next discovery update";
                        }
                    });
                    
                    connect(directChannel, &network::WsChannel::textMessageReceived, this, [this, clientId = client.clientId](const QString& message) {
                        handleDirectClientMessage(clientId, message);
                    });
                    
                    connect(directChannel, &network::WsChannel::binaryMessageReceived, this, [this, clientId = client.clientId](const QByteArray& data) {
                        handleDirectClientBinary(clientId, data);
                    });
                    
                    // 连接到StreamClient的控制端�?                    QUrl controlUrl(client.controlUrl);
                    if (controlUrl.isValid()) {
                        qInfo() << "[Console] Auto-connecting WebSocket to" << client.clientId << "at" << controlUrl.toString();
                        directChannel->connectTo(controlUrl);
                    }
                }
            }
        }
    });
    connect(clientDiscovery_.get(), &ClientDiscovery::clientUpdated, this, [this, clientListRefreshTimer](const DiscoveredClient& client) {
        clientListRefreshTimer->start();  // 防抖刷新
        // 如果客户端正在监控但WebSocket未连接，尝试自动重连（带防抖�?        if (activePlayers_.contains(client.clientId)) {
            auto itChannel = directControlChannels_.find(client.clientId);
            // 检查连接状态：如果连接存在且已连接，不需要重�?            if (itChannel != directControlChannels_.end() && itChannel.value() && itChannel.value()->isConnected()) {
                return;  // 已连接，不需要重�?            }
            
            // 如果正在连接中，跳过
            if (connectingClients_.contains(client.clientId)) {
                return;
            }
            
            // 使用防抖定时器，避免频繁重连
            auto itTimer = reconnectTimers_.find(client.clientId);
            if (itTimer == reconnectTimers_.end()) {
                QTimer* timer = new QTimer(this);
                timer->setSingleShot(true);
                timer->setInterval(2000);  // 2秒防�?                reconnectTimers_[client.clientId] = timer;
                
                connect(timer, &QTimer::timeout, this, [this, client]() {
                    reconnectTimers_.remove(client.clientId);
                    if (!activePlayers_.contains(client.clientId)) {
                        return;  // 客户端已停止监控
                    }
                    
                    // 再次检查连接状�?                    auto itChannel = directControlChannels_.find(client.clientId);
                    if (itChannel != directControlChannels_.end() && itChannel.value() && itChannel.value()->isConnected()) {
                        return;  // 已连接，不需要重�?                    }
                    
                    if (connectingClients_.contains(client.clientId)) {
                        return;  // 正在连接�?                    }
                    
                    // 执行重连
                    performWebSocketReconnect(client);
                });
            }
            
            // 重启防抖定时�?            reconnectTimers_[client.clientId]->start();
        }
    });
    connect(clientDiscovery_.get(), &ClientDiscovery::clientExpired, this, [this, clientListRefreshTimer](const QString& clientId) {
        qInfo() << "[Console] Client expired:" << clientId;
        clientListRefreshTimer->start();  // 防抖刷新
    });
    clientDiscovery_->start(40810);  // 启动UDP监听
    
    // 兼容模式：保留WebSocket连接（可选，用于向后兼容�?    controlChannel_ = std::make_unique<network::WsChannel>(this);
    const QUrl controlUrl(config_.streamControlUrl());
    if (!controlUrl.isValid()) {
        qWarning() << "[Console] Control URL invalid:" << config_.streamControlUrl();
        return;
    }

    connect(controlChannel_.get(), &network::WsChannel::connected, this, [this]() {
        handleStatusChanged(QStringLiteral("Control Connected"));
        requestClientList();
        for (auto it = activePlayers_.cbegin(); it != activePlayers_.cend(); ++it) {
            const auto& player = it.value();
            sendSubscribe(it.key(), player->ssrc(), player->localPort());
        }
    });

    connect(controlChannel_.get(), &network::WsChannel::disconnected, this, [this]() {
        handleStatusChanged(QStringLiteral("Control Disconnected"));
    });

    connect(controlChannel_.get(), &network::WsChannel::textMessageReceived,
            this, &MainWindow::handleControlText);
    
    connect(controlChannel_.get(), &network::WsChannel::frameReceived,
            this, [this](const network::Frame& frame) {
                const QString type = frame.header.value(QStringLiteral("type")).toString();
                if (type == QStringLiteral("alert")) {
                    // 处理报警 Frame 消息
                    const QString payload = QString::fromUtf8(frame.payload);
                    handleControlText(payload);
                } else if (type == QStringLiteral("screenshot_uploaded")) {
                    // 处理截图上传完成 Frame 消息
                    const QString payload = QString::fromUtf8(frame.payload);
                    handleControlText(payload);
                }
            });

    controlChannel_->connectTo(controlUrl);
}

void MainWindow::handleStatusChanged(const QString& status) {
    if (statusLabel_) {
        statusLabel_->setText(status);
    }
}

void MainWindow::handleClientItemActivated(QTreeWidgetItem* item, int) {
    if (!item || item->data(0, kRoleType).toInt() != kItemTypeClient) {
        return;
    }
    const QString clientId = item->data(0, kRoleClientId).toString();
    auto entryIt = clientEntries_.find(clientId);
    if (entryIt == clientEntries_.end()) {
        return;
    }
    if (!entryIt->online) {
        statusBar()->showMessage(tr("客户�?%1 已离�?).arg(clientId), 3000);
        return;
    }
    const quint32 ssrc = entryIt->ssrc;
    if (activePlayers_.contains(clientId)) {
        stopPreview(clientId);
    } else {
        startPreview(clientId, ssrc);
    }
}

void MainWindow::handleClientContextMenu(const QPoint& pos) {
    QPoint viewportPos = clientTree_->viewport()->mapFrom(clientTree_, pos);
    QTreeWidgetItem* item = clientTree_->itemAt(viewportPos);
    QMenu menu(this);

    QAction* refreshTreeAction = nullptr;

    if (!item) {
        QAction* addGroupAction = menu.addAction(tr("新增分组"));
        refreshTreeAction = menu.addAction(tr("刷新客户端列�?));
        menu.addSeparator();
        QAction* clearAllDataAction = menu.addAction(tr("🗑�?记录初始化（清除所有数据）"));
        const QAction* chosen = menu.exec(clientTree_->viewport()->mapToGlobal(viewportPos));
        if (chosen == addGroupAction) {
            addGroup();
        } else if (chosen == refreshTreeAction) {
            requestClientList();
        } else if (chosen == clearAllDataAction) {
            handleClearAllData();
        }
        return;
    }

    refreshTreeAction = menu.addAction(tr("刷新客户端列�?));
    menu.addSeparator();

    const int itemType = item->data(0, kRoleType).toInt();
    if (itemType == kItemTypeGroup) {
        QAction* addGroupAction = menu.addAction(tr("新增分组"));
        QAction* renameGroupAction = nullptr;
        QAction* removeGroupAction = nullptr;
        const QString groupName = item->data(0, kRoleGroupName).toString();
        if (groupName != DefaultGroup()) {
            renameGroupAction = menu.addAction(tr("重命名分�?));
            removeGroupAction = menu.addAction(tr("删除分组"));
        }
        const QAction* chosen = menu.exec(clientTree_->viewport()->mapToGlobal(viewportPos));
        if (chosen == addGroupAction) {
            addGroup();
        } else if (chosen == renameGroupAction) {
            renameGroup(item);
        } else if (chosen == removeGroupAction) {
            removeGroup(item);
        } else if (chosen == refreshTreeAction) {
            requestClientList();
        }
        return;
    }

    if (itemType != kItemTypeClient) {
        return;
    }

    const QString clientId = item->data(0, kRoleClientId).toString();
    const ClientEntry entry = clientEntries_.value(clientId);
    const bool active = activePlayers_.contains(clientId);
    const bool online = entry.online;

    QAction* startAction = menu.addAction(tr("开始监�?));
    QAction* stopAction = menu.addAction(tr("停止监控"));
    startAction->setEnabled(!active && online);
    stopAction->setEnabled(active);

    QAction* remarkAction = menu.addAction(tr("编辑备注"));
    QAction* detailAction = menu.addAction(tr("查看详情"));

    QMenu* moveMenu = menu.addMenu(tr("移动到分�?));
    QString currentGroup = item->data(0, kRoleGroupName).toString();
    QStringList groups = groupNames_.values();
    groups.removeAll(DefaultGroup());
    std::sort(groups.begin(), groups.end(), [](const QString& a, const QString& b) {
        return a.localeAwareCompare(b) < 0;
    });
    groups.prepend(DefaultGroup());
    QList<QAction*> moveActions;
    for (const QString& group : groups) {
        QAction* act = moveMenu->addAction(group);
        act->setData(group);
        act->setEnabled(group != currentGroup);
        moveActions.append(act);
    }

    const QAction* chosen = menu.exec(clientTree_->viewport()->mapToGlobal(viewportPos));
    if (!chosen) {
        return;
    }
    if (chosen == refreshTreeAction) {
        requestClientList();
    } else if (chosen == startAction) {
        startPreview(clientId, entry.ssrc);
    } else if (chosen == stopAction) {
        stopPreview(clientId);
    } else if (chosen == remarkAction) {
        editClientRemark(clientId);
    } else if (chosen == detailAction) {
        openClientDetails(clientId);
    } else if (moveActions.contains(const_cast<QAction*>(chosen))) {
        const QString newGroup = chosen->data().toString();
        handleClientDropped(clientId, newGroup);
    }
}

void MainWindow::handleTileDropped(const QString& targetId, const QString& sourceId) {
    if (layoutLocked_) {
        return;
    }
    if (sourceId == targetId) {
        return;
    }
    const int srcIndex = layoutOrder_.indexOf(sourceId);
    int dstIndex = layoutOrder_.indexOf(targetId);
    if (srcIndex == -1 || dstIndex == -1) {
        return;
    }

    layoutOrder_.removeAt(srcIndex);
    if (srcIndex < dstIndex) {
        --dstIndex;
    }
    layoutOrder_.insert(dstIndex, sourceId);
    statusBar()->showMessage(tr("已调整监控顺�?), 1500);
    rebuildPreviewLayout();
}

void MainWindow::updateTileDragEnabled() {
    const bool enabled = !layoutLocked_ && !wallFullscreen_;
    for (auto it = activeTiles_.begin(); it != activeTiles_.end(); ++it) {
        it.value()->setDragEnabled(enabled);
    }
    if (lockLayoutAction_) {
        lockLayoutAction_->setChecked(layoutLocked_);
    }
}

void MainWindow::handleTileContextMenu(StreamTile* tile, const QPoint& globalPos) {
    if (!tile) {
        return;
    }
    const QString clientId = tile->clientId();
    const ClientEntry entry = clientEntries_.value(clientId);
    const bool active = activePlayers_.contains(clientId);
    const bool online = entry.online;
    QMenu menu(this);

    QAction* fullscreenAction = menu.addAction(tr("全屏查看"));
    QAction* wallAction = menu.addAction(wallFullscreen_ ? tr("退出监控墙全屏")
                                                         : tr("进入监控墙全�?));
    QAction* detailAction = menu.addAction(tr("查看详情"));
    
    // 视频录制控制
    menu.addSeparator();
    StreamPlayer* player = activePlayers_.value(clientId, nullptr);
    QAction* startRecordAction = nullptr;
    QAction* stopRecordAction = nullptr;
    if (player) {
        if (player->isRecording()) {
            stopRecordAction = menu.addAction(tr("�?停止录制"));
        } else {
            startRecordAction = menu.addAction(tr("🔴 开始录�?));
        }
    }
    
    // 查看视频记录
    QAction* viewRecordsAction = menu.addAction(tr("📹 查看视频记录"));
    
    menu.addSeparator();
    QAction* refreshPreviewAction = nullptr;
    if (active) {
        refreshPreviewAction = menu.addAction(tr("刷新当前窗口"));
    }

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) {
        return;
    }

    if (chosen == fullscreenAction) {
        openFullscreenView(clientId);
        return;
    }
    if (chosen == wallAction) {
        setWallFullscreen(!wallFullscreen_);
        return;
    }
    if (chosen == detailAction) {
        openClientDetails(clientId);
        return;
    }
    if (chosen == startRecordAction && player) {
        QString hostname = clientId;
        if (!entry.remark.isEmpty()) {
            hostname = entry.remark;
        }
        const QString savePath = videoSavePath_.isEmpty() 
            ? (QCoreApplication::applicationDirPath() + QStringLiteral("/recordings"))
            : videoSavePath_;
        player->startRecording(hostname, savePath, videoSaveDurationHours_);
        recordingDisabledClients_.remove(clientId);  // 从禁用列表中移除
        statusBar()->showMessage(tr("开始录制客户端 %1 的视频流").arg(clientId), 3000);
        return;
    }
    if (chosen == stopRecordAction && player) {
        player->stopRecording();
        recordingDisabledClients_.insert(clientId);  // 添加到禁用列�?        statusBar()->showMessage(tr("停止录制客户�?%1 的视频流").arg(clientId), 3000);
        return;
    }
    if (chosen == viewRecordsAction) {
        handleViewVideoRecords(clientId);
        return;
    }
    if (chosen == refreshPreviewAction && active) {
        quint32 ssrc = entry.ssrc != 0 ? entry.ssrc : tile->ssrc();
        stopPreview(clientId);
        if (entry.online && ssrc != 0) {
            QTimer::singleShot(100, this, [this, clientId, ssrc]() {
                startPreview(clientId, ssrc);
            });
        }
        return;
    }
}

void MainWindow::rebuildToolbar() {
    QToolBar* toolBar = findChild<QToolBar*>(QStringLiteral("main_toolbar"));
    if (!toolBar) {
        return;
    }
    toolBar->clear();
    layoutPresetActions_.clear();

    auto addActionWithData = [&](const QString& text, const QVariant& data) {
        QAction* act = toolBar->addAction(text);
        act->setData(data);
        return act;
    };

    // 创建布局预设动作（用于右键菜单，不在工具栏显示）
    auto addPreset = [&](int rows, const QString& id, const QString& text) {
        QAction* act = new QAction(text, this);
        act->setData(QStringLiteral("preset:%1").arg(id));
        act->setCheckable(true);
        act->setProperty("layoutRows", rows);
        layoutPresetActions_.append(act);
        connect(act, &QAction::triggered, this, &MainWindow::applyLayoutPresetFromAction);
        return act;
    };

    const QVector<int> rowOptions = {
        1, 2, 3, 4,
        5, 6, 7, 8,
        9, 10, 11, 12,
        13, 14, 15, 16,
        17, 18, 19, 20};

    const QString timesChar = QString::fromUtf16(u"×");
    // 创建布局预设动作（仅用于右键菜单，不在工具栏显示�?    for (int index = 0; index < rowOptions.size(); ++index) {
        const int rows = rowOptions.at(index);
        const QString id = QStringLiteral("%1x4").arg(rows);
        const QString label = QStringLiteral("%1%2%3").arg(rows).arg(timesChar).arg(4);
        addPreset(rows, id, label);
    }

    // 工具栏只保留锁定布局和监控墙全屏按钮
    lockLayoutAction_ = addActionWithData(tr("锁定布局"), QStringLiteral("layout:lock"));
    lockLayoutAction_->setCheckable(true);
    lockLayoutAction_->setChecked(layoutLocked_);

    wallFullscreenAction_ = addActionWithData(tr("监控墙全�?), QStringLiteral("view:wall_fullscreen"));
    wallFullscreenAction_->setCheckable(true);
    wallFullscreenAction_->setChecked(wallFullscreen_);

    connect(toolBar,
            &QToolBar::actionTriggered,
            this,
            [this](QAction* action) {
                const QString cmd = action->data().toString();
                if (cmd.isEmpty()) {
                    return;
                }
                if (cmd == QStringLiteral("layout:lock")) {
                    layoutLocked_ = action->isChecked();
                    updateTileDragEnabled();
                    statusBar()->showMessage(layoutLocked_ ? tr("布局已锁�?) : tr("布局已解�?), 2000);
                    return;
                }
                if (cmd == QStringLiteral("view:wall_fullscreen")) {
                    toggleWallFullscreen();
                    return;
                }
            });
    updateLayoutActions();
}

void MainWindow::handleControlText(const QString& payload) {
    const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8());
    if (!doc.isObject()) {
        qWarning() << "[Console] Invalid control message" << payload;
        return;
    }
    const QJsonObject obj = doc.object();
    const QString action = obj.value(QStringLiteral("action")).toString();
    const QString status = obj.value(QStringLiteral("status")).toString();

    if ((action == QStringLiteral("list") || action == QStringLiteral("client_list"))
        && (status.isEmpty() || status == QStringLiteral("ok"))) {
        const QJsonArray clients = obj.value(QStringLiteral("clients")).toArray();
        refreshClientModel(clients);
    } else if (action == QStringLiteral("subscribe") && status == QStringLiteral("ok")) {
        statusBar()->showMessage(tr("已订阅流 SSRC %1").arg(obj.value(QStringLiteral("ssrc")).toInt()), 2000);
    } else if (action == QStringLiteral("unsubscribe") && status == QStringLiteral("ok")) {
        statusBar()->showMessage(tr("已取消订阅流 SSRC %1").arg(obj.value(QStringLiteral("ssrc")).toInt()), 2000);
    } else if (action == QStringLiteral("alert")) {
        // 处理报警消息，自动记录并通知管理端（无论是否打开对话框）
        const QJsonObject detection = obj.value(QStringLiteral("detection")).toObject();
        const QString clientId = obj.value(QStringLiteral("client_id")).toString();
        
        qInfo() << "[Console] Received alert message for clientId=" << clientId;
        
        if (clientId.isEmpty()) {
            return;
        }
        
        // 提取报警信息
        const QString keyword = detection.value(QStringLiteral("word")).toString();
        const QString windowTitle = detection.value(QStringLiteral("window_title")).toString();
        const QString context = detection.value(QStringLiteral("context")).toString();
        
        // 纯UDP模式：直接保存到本地数据�?        if (db_.isValid()) {
            const QDateTime timestamp = QDateTime::fromString(
                detection.value(QStringLiteral("timestamp")).toString(), Qt::ISODate);
            const QString screenshotPath = detection.value(QStringLiteral("screenshot_path")).toString();
            
            db_->insertAlert(clientId, keyword, windowTitle, context, 
                           screenshotPath.isEmpty() ? QString() : screenshotPath, timestamp);
        }
        
        // 获取客户端显示名称（备注或ID�?        QString displayName = clientId;
        auto entryIt = clientEntries_.find(clientId);
        if (entryIt != clientEntries_.end() && !entryIt->remark.isEmpty()) {
            displayName = QStringLiteral("%1 (%2)").arg(clientId, entryIt->remark);
        }
        
        // 在主界面显示通知（无论是否打开对话框）
        QString alertMessage = tr("【报警】客户端 %1 检测到敏感词：%2").arg(displayName, keyword);
        if (!windowTitle.isEmpty()) {
            alertMessage += tr(" (窗口�?1)").arg(windowTitle);
        }
        statusBar()->showMessage(alertMessage, 10000);  // 显示10�?        
        // 如果当前打开了该客户端的详情对话框，则刷新报警列表和截图列表
        if (activeDetailsDialog_ && !activeDetailsDialog_.isNull()) {
            if (activeDetailsDialog_->clientId() == clientId) {
                qInfo() << "[Console] Refreshing alerts for active dialog, clientId=" << clientId;
                activeDetailsDialog_->refreshAlertsAndScreenshots();
            }
        }
        
        // 可选：显示系统通知（如果系统支持）
        #if defined(Q_OS_WIN)
        // Windows 系统通知可以通过 QSystemTrayIcon 实现，但当前没有实现
        #endif
        
    } else if (action == QStringLiteral("screenshot_uploaded")) {
        // 处理截图上传完成消息，自动通知管理端（无论是否打开对话框）
        const QString clientId = obj.value(QStringLiteral("client_id")).toString();
        const QString screenshotFileName = obj.value(QStringLiteral("screenshot")).toString();
        
        qInfo() << "[Console] Received screenshot upload notification for clientId=" << clientId << "screenshot:" << screenshotFileName;
        
        if (clientId.isEmpty()) {
            return;
        }
        
        // 获取客户端显示名�?        QString displayName = clientId;
        auto entryIt = clientEntries_.find(clientId);
        if (entryIt != clientEntries_.end() && !entryIt->remark.isEmpty()) {
            displayName = QStringLiteral("%1 (%2)").arg(clientId, entryIt->remark);
        }
        
        // 在主界面显示通知（无论是否打开对话框）
        statusBar()->showMessage(tr("【截图】客户端 %1 上传了新截图�?2").arg(displayName, screenshotFileName), 5000);
        
        // 如果当前打开了该客户端的详情对话框，则刷新截图列�?        if (activeDetailsDialog_ && !activeDetailsDialog_.isNull()) {
            if (activeDetailsDialog_->clientId() == clientId) {
                qInfo() << "[Console] Refreshing screenshots for active dialog, clientId=" << clientId;
                activeDetailsDialog_->refreshAlertsAndScreenshots();
            }
        }
    }
}

void MainWindow::handleDirectClientMessage(const QString& clientId, const QString& message) {
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) {
        qWarning() << "[Console] Invalid direct client message from" << clientId << ":" << message.left(100);
        return;
    }
    
    const QJsonObject obj = doc.object();
    const QString action = obj.value(QStringLiteral("action")).toString();
    
    if (action == QStringLiteral("app_usage")) {
        // 存储应用使用统计
        const QJsonArray apps = obj.value(QStringLiteral("apps")).toArray();
        clientAppUsageData_[clientId] = apps;
        // 移除日志:频繁数据接收会影响性能
        
        // 纯UDP模式:直接保存到本地数据库
        if (db_.isValid()) {
            for (const QJsonValue& usage : apps) {
                const QJsonObject usageObj = usage.toObject();
                const QString appName = usageObj.value(QStringLiteral("app_name")).toString();
                const qint64 totalSec = static_cast<qint64>(usageObj.value(QStringLiteral("total_sec")).toDouble());
                const QDateTime timestamp = QDateTime::fromString(
                    usageObj.value(QStringLiteral("timestamp")).toString(), Qt::ISODate);
                
                QSqlQuery query(db_);
                query.prepare(QStringLiteral(
                    "INSERT INTO app_usage (client_id, app_name, total_seconds, timestamp) "
                    "VALUES (:client_id, :app_name, :total_seconds, :timestamp)"));
                query.bindValue(QStringLiteral(":client_id"), clientId);
                query.bindValue(QStringLiteral(":app_name"), appName);
                query.bindValue(QStringLiteral(":total_seconds"), totalSec);
                query.bindValue(QStringLiteral(":timestamp"), timestamp.toString(Qt::ISODate));
                query.exec();
            }
        }
        
        // 如果当前打开了该客户端的详情对话框，刷新应用统计
        if (activeDetailsDialog_ && !activeDetailsDialog_.isNull() && activeDetailsDialog_->clientId() == clientId) {
            activeDetailsDialog_->refreshAllData();
        }
    } else if (action == QStringLiteral("activities")) {
        // 存储活动数据（批量）
        const QJsonArray activities = obj.value(QStringLiteral("activities")).toArray();
        QJsonArray& existing = clientActivitiesData_[clientId];
        for (const QJsonValue& activity : activities) {
            existing.append(activity);
        }
        // 移除日志：频繁数据接收会影响性能
        
        // 纯UDP模式：直接保存到本地数据�?        if (db_.isValid()) {
            for (const QJsonValue& activity : activities) {
                const QJsonObject actObj = activity.toObject();
                const QString appName = actObj.value(QStringLiteral("app_name")).toString();
                const QString windowTitle = actObj.value(QStringLiteral("window_title")).toString();
                const qint64 durationSec = static_cast<qint64>(actObj.value(QStringLiteral("duration_sec")).toDouble());
                const QDateTime timestamp = QDateTime::fromString(
                    actObj.value(QStringLiteral("timestamp")).toString(), Qt::ISODate);
                db_->insertActivity(clientId, appName, windowTitle, durationSec, timestamp);
            }
        }
        
        // 如果当前打开了该客户端的详情对话框，刷新活动日志
        if (activeDetailsDialog_ && !activeDetailsDialog_.isNull() && activeDetailsDialog_->clientId() == clientId) {
            activeDetailsDialog_->refreshAllData();
        }
    } else if (action == QStringLiteral("activity")) {
        // 存储单个活动数据
        const QJsonArray activities = obj.value(QStringLiteral("activities")).toArray();
        if (!activities.isEmpty()) {
            QJsonArray& existing = clientActivitiesData_[clientId];
            existing.append(activities.first());
            // 移除日志：频繁活动接收会影响性能
            
            // 完全直连模式：转发到CommandController保存到数据库
            QString restUrl = config_.restApiUrl();
            if (!restUrl.isEmpty() && restApiManager_) {
                QUrl url(restUrl);
                if (url.isValid()) {
                    url.setPath(QStringLiteral("/api/client/%1/activities").arg(clientId));
                    QNetworkRequest request(url);
                    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
                    
                    QJsonObject payloadObj;
                    payloadObj.insert(QStringLiteral("activities"), activities);
                    QJsonDocument payloadDoc(payloadObj);
                    
                    QNetworkReply* reply = restApiManager_->post(request, payloadDoc.toJson(QJsonDocument::Compact));
                    connect(reply, &QNetworkReply::finished, this, [this, reply, clientId]() {
                        if (reply->error() == QNetworkReply::NoError) {
                            // 移除日志：频繁保存成功消息会影响性能
                        } else {
                            qWarning() << "[Console] �?Failed to save single activity to database for clientId=" << clientId << "error:" << reply->errorString();
                        }
                        reply->deleteLater();
                    });
                }
            }
            
            // 优化：如果当前打开了该客户端的详情对话框，自动刷新活动日志
            if (activeDetailsDialog_ && !activeDetailsDialog_.isNull() && activeDetailsDialog_->clientId() == clientId) {
                activeDetailsDialog_->refreshActivities();
            }
        }
    } else if (action == QStringLiteral("screenshot")) {
        // 存储截图元数据（二进制数据会在handleDirectClientBinary中接收）
        // 使用队列避免多个截图时元数据被覆�?        pendingScreenshotMetadata_[clientId].enqueue(message);
        // 移除日志：频繁截图接收会影响性能
    } else if (action == QStringLiteral("alert")) {
        // 报警消息已经在handleControlText中处理，这里也调用以保持一致�?        handleControlText(message);
    } else {
        qDebug() << "[Console] Unhandled direct client message action:" << action << "from" << clientId;
    }
}

void MainWindow::handleDirectClientBinary(const QString& clientId, const QByteArray& data) {
    // 检查是否有待处理的截图元数据（使用队列避免覆盖�?    if (pendingScreenshotMetadata_.contains(clientId) && !pendingScreenshotMetadata_[clientId].isEmpty()) {
        const QString metadataJson = pendingScreenshotMetadata_[clientId].dequeue();
        const QJsonDocument doc = QJsonDocument::fromJson(metadataJson.toUtf8());
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            const QString timestamp = obj.value(QStringLiteral("timestamp")).toString();
            const QString type = obj.value(QStringLiteral("type")).toString();  // "alert" �?"window_change"
            const QString keyword = obj.value(QStringLiteral("keyword")).toString();
            const QString label = obj.value(QStringLiteral("label")).toString();
            const QString context = obj.value(QStringLiteral("context")).toString();
            const QString windowTitle = obj.value(QStringLiteral("window_title")).toString();
            const QString appName = obj.value(QStringLiteral("app_name")).toString();
            const QString detectionType = obj.value(QStringLiteral("detection_type")).toString();
            
            if (!timestamp.isEmpty()) {
                // 存储截图数据（用于本地显示）
                clientScreenshotsData_[clientId][timestamp] = data;
                
                // 完全直连模式：DesktopConsole 直接保存截图文件到本�?                const QString savedPath = saveScreenshotFileDirect(clientId, data, timestamp, type == QStringLiteral("alert"));
                if (!savedPath.isEmpty()) {
                    // 纯UDP模式：直接保存到本地数据�?                    if (db_.isValid()) {
                        const QDateTime ts = QDateTime::fromString(timestamp, Qt::ISODate);
                        db_->insertScreenshot(clientId, savedPath, ts, 
                            type == QStringLiteral("alert"),
                            keyword, windowTitle);
                    }
                } else {
                    qWarning() << "[Console] �?Failed to save screenshot file for clientId=" << clientId;
                }
                
                // 如果当前打开了该客户端的详情对话框，刷新截图列表
                if (activeDetailsDialog_ && !activeDetailsDialog_.isNull() && activeDetailsDialog_->clientId() == clientId) {
                    activeDetailsDialog_->refreshAlertsAndScreenshots();
                }
            } else {
                qWarning() << "[Console] Screenshot metadata missing timestamp:" << metadataJson.left(100);
            }
        } else {
            qWarning() << "[Console] Invalid screenshot metadata JSON:" << metadataJson.left(100);
        }
    } else {
        qWarning() << "[Console] Received binary data from" << clientId << "but no pending metadata (queue empty or not exists), size:" << data.size();
    }
}

QJsonArray MainWindow::getClientAppUsage(const QString& clientId) const {
    return clientAppUsageData_.value(clientId);
}

QJsonArray MainWindow::getClientActivities(const QString& clientId) const {
    return clientActivitiesData_.value(clientId);
}

QMap<QString, QByteArray> MainWindow::getClientScreenshots(const QString& clientId) const {
    return clientScreenshotsData_.value(clientId);
}

void MainWindow::requestClientList() {
    // 完全直连方案：通过UDP发现获取客户端列�?    if (clientDiscovery_) {
        const QList<DiscoveredClient> clients = clientDiscovery_->discoveredClients();
        QJsonArray items;
        for (const DiscoveredClient& client : clients) {
            QJsonObject item;
            item.insert(QStringLiteral("client_id"), client.clientId);
            item.insert(QStringLiteral("ssrc"), static_cast<qint64>(client.ssrc));
            item.insert(QStringLiteral("ip"), client.ip);
            item.insert(QStringLiteral("rtp_port"), static_cast<int>(client.rtpPort));
            item.insert(QStringLiteral("control_url"), client.controlUrl);
            items.append(item);
        }
        refreshClientModel(items);
    }
    
    // 兼容模式：如果WebSocket连接存在且已连接，也请求（向后兼容）
    if (controlChannel_ && controlChannel_->isConnected()) {
        QJsonObject obj{{"action", QStringLiteral("list")}};
        controlChannel_->sendText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    }
    
    // 纯UDP架构：移�?REST API 调用，客户端信息通过 UDP 心跳和数据库查询获取
    // static QTimer* restApiTimer = nullptr;
    // if (!restApiTimer) {
    //     restApiTimer = new QTimer(this);
    //     restApiTimer->setSingleShot(true);
    //     restApiTimer->setInterval(3000);
    //     connect(restApiTimer, &QTimer::timeout, this, &MainWindow::fetchClientsFromRestApi);
    // }
    // restApiTimer->start();
}

void MainWindow::refreshClientModel(const QJsonArray& items) {
    // 移除日志：频繁刷新会影响性能，仅在客户端数量变化时输�?    static int lastClientCount = -1;
    if (items.size() != lastClientCount) {
        qInfo() << "[Console] Client list updated:" << items.size() << "entries";
        lastClientCount = items.size();
    }

    for (auto it = clientEntries_.begin(); it != clientEntries_.end(); ++it) {
        it->online = false;
    }

    QSet<QString> seen;

    for (const QJsonValue& value : items) {
        const QJsonObject obj = value.toObject();
        const QString clientId = obj.value(QStringLiteral("client_id")).toString();
        const quint32 ssrc = static_cast<quint32>(obj.value(QStringLiteral("ssrc")).toVariant().toULongLong());
        if (clientId.isEmpty()) {
            continue;
        }
        
        // Extract remark from REST API response if available
        const QString serverRemark = obj.value(QStringLiteral("remark")).toString();
        if (!serverRemark.isEmpty()) {
            clientRemarksCache_.insert(clientId, serverRemark);
        }
        
        ClientEntry& entry = clientEntries_[clientId];
        if (ssrc != 0) {
            entry.ssrc = ssrc;
            entry.online = true;
        } else {
            // If no SSRC, check if we already have one
            if (entry.ssrc == 0) {
                // Keep existing online status if we have SSRC from previous snapshot
                // Otherwise mark as offline
                entry.online = false;
            }
        }
        entry.group = clientGroupsCache_.value(clientId, entry.group.isEmpty() ? DefaultGroup() : entry.group);
        entry.remark = clientRemarksCache_.value(clientId, entry.remark);
        groupNames_.insert(entry.group.isEmpty() ? DefaultGroup() : entry.group);
        seen.insert(clientId);
    }

    // Ensure metadata-only clients appear as offline entries
    for (auto it = clientGroupsCache_.cbegin(); it != clientGroupsCache_.cend(); ++it) {
        if (!clientEntries_.contains(it.key())) {
            ClientEntry entry;
            entry.group = it.value();
            entry.remark = clientRemarksCache_.value(it.key());
            entry.online = false;
            entry.ssrc = 0;
            clientEntries_.insert(it.key(), entry);
        }
    }
    for (auto it = clientRemarksCache_.cbegin(); it != clientRemarksCache_.cend(); ++it) {
        if (!clientEntries_.contains(it.key())) {
            ClientEntry entry;
            entry.group = clientGroupsCache_.value(it.key(), DefaultGroup());
            entry.remark = it.value();
            entry.online = false;
            entry.ssrc = 0;
            clientEntries_.insert(it.key(), entry);
        }
    }

    scheduleClientTreeRebuild();  // 批量更新，避免频繁重�?    populateGroupFilterOptions();
    syncPreviewWithFilter();
    updateWallHeaderStats();
}

void MainWindow::startPreview(const QString& clientId, quint32 ssrc) {
    if (activePlayers_.contains(clientId)) {
        return;
    }
    if (!controlChannel_) {
        qWarning() << "[Console] Control channel not ready";
        return;
    }

    auto entryIt = clientEntries_.find(clientId);
    if (entryIt != clientEntries_.end()) {
        if (!entryIt->online) {
            qWarning() << "[Console] Client" << clientId << "is offline, skip preview.";
            return;
        }
        if (ssrc == 0) {
            ssrc = entryIt->ssrc;
        } else {
            entryIt->ssrc = ssrc;
        }
    } else {
        if (ssrc == 0) {
            qWarning() << "[Console] Start preview missing SSRC for" << clientId;
            return;
        }
        ClientEntry entry;
        entry.online = true;
        entry.ssrc = ssrc;
        entry.group = clientGroupsCache_.value(clientId, DefaultGroup());
        entry.remark = clientRemarksCache_.value(clientId);
        entryIt = clientEntries_.insert(clientId, entry);
    }

    entryIt->group = groupForClient(clientId);
    entryIt->remark = remarkForClient(clientId);

    if (!layoutOrder_.contains(clientId)) {
        layoutOrder_.append(clientId);
    }

    layoutOrder_.removeAll(clientId);
    layoutOrder_.append(clientId);

    qInfo() << "[Console] Start preview for" << clientId << "ssrc" << ssrc;
    auto* tile = new StreamTile(clientId, ssrc, previewContainer_);
    connect(tile, &StreamTile::aspectRatioChanged, this, [this](const QString&) {
        schedulePreviewRelayout();
    });
    tile->setStats(0.0, 0.0);
    tile->setErrorMessage(QString());
    connect(tile, &StreamTile::contextMenuRequested, this, &MainWindow::handleTileContextMenu);
    connect(tile, &StreamTile::tileDropped, this, &MainWindow::handleTileDropped);
    connect(tile, &StreamTile::tileDoubleClicked, this, &MainWindow::openFullscreenView);
    activeTiles_.insert(clientId, tile);
    tile->setDragEnabled(!layoutLocked_ && !wallFullscreen_);

    auto* player = new StreamPlayer(clientId, ssrc, tile, this);
    connect(player, &StreamPlayer::statsUpdated, this, &MainWindow::updateTileStats);
    connect(player, &StreamPlayer::decodingFailed, this, [this](const QString& id, const QString&) {
        updateTileStats(id, 0.0, 0.0, tr("解码失败"));
    });
    if (!player->start()) {
        qWarning() << "[Console] StreamPlayer start failed for" << clientId;
        activeTiles_.remove(clientId);
        delete player;
        tile->deleteLater();
        return;
    }

    const quint16 port = player->localPort();
    activePlayers_.insert(clientId, player);
    
    // 全自动录制：如果配置了视频保存路径，且该客户端未被手动停止录制，则自动开始录�?    if (!videoSavePath_.isEmpty() && !recordingDisabledClients_.contains(clientId)) {
        QString hostname = entryIt->remark.isEmpty() ? clientId : entryIt->remark;
        player->startRecording(hostname, videoSavePath_, videoSaveDurationHours_);
    }
    
    updateTileDisplayName(clientId);
    rebuildPreviewLayout();
    
    // 完全直连方案：通过WebSocket直连StreamClient，告知自己的IP和端�?    // 连接复用：检查是否已存在连接，避免重复创�?    if (clientDiscovery_) {
        DiscoveredClient discoveredClient = clientDiscovery_->client(clientId);
        if (!discoveredClient.clientId.isEmpty() && !discoveredClient.controlUrl.isEmpty()) {
            network::WsChannel* directChannel = nullptr;
            
            // 检查是否已存在连接
            auto itChannel = directControlChannels_.find(clientId);
            if (itChannel != directControlChannels_.end() && itChannel.value() != nullptr) {
                directChannel = itChannel.value();
                // 如果连接已存在且已连接，复用�?                if (directChannel->isConnected()) {
                    qInfo() << "[Console] Reusing existing WebSocket connection to" << clientId;
                    // 直接发送订阅消息（连接已存在）
                    sendDirectSubscribe(clientId, ssrc, port);
                    return;  // 复用连接，不需要重新连�?                } else {
                    // 连接存在但未连接，清理旧连接
                    qInfo() << "[Console] Existing WebSocket connection to" << clientId << "is not connected, cleaning up";
                    directChannel->deleteLater();
                    directControlChannels_.remove(clientId);
                    directChannel = nullptr;
                }
            }
            
            // 创建新的WebSocket连接
            if (!directChannel) {
                directChannel = new network::WsChannel(this);
                directControlChannels_[clientId] = directChannel;
                
                connect(directChannel, &network::WsChannel::connected, this, [this, clientId, ssrc, port]() {
                    connectingClients_.remove(clientId);  // 连接成功，移除标�?                    qInfo() << "[Console] Direct WebSocket connected to" << clientId;
                    // 发送订阅消�?                    sendDirectSubscribe(clientId, ssrc, port);
                });
                
                connect(directChannel, &network::WsChannel::disconnected, this, [this, clientId]() {
                    connectingClients_.remove(clientId);  // 连接断开，移除标�?                    qWarning() << "[Console] Direct WebSocket disconnected from" << clientId;
                    // 断开连接时，清理旧连接，等待自动重连
                    auto it = directControlChannels_.find(clientId);
                    if (it != directControlChannels_.end()) {
                        it.value()->deleteLater();
                        directControlChannels_.erase(it);
                    }
                    // 如果客户端仍在监控列表中，尝试自动重连（通过防抖定时器）
                    if (activePlayers_.contains(clientId)) {
                        qInfo() << "[Console] Client" << clientId << "is still being monitored, will attempt reconnect on next discovery update";
                    }
                });
                
                // 完全直连模式：接收StreamClient发送的数据（app_usage, activities, screenshot等）
                connect(directChannel, &network::WsChannel::textMessageReceived, this, [this, clientId](const QString& message) {
                    handleDirectClientMessage(clientId, message);
                });
                
                connect(directChannel, &network::WsChannel::binaryMessageReceived, this, [this, clientId](const QByteArray& data) {
                    handleDirectClientBinary(clientId, data);
                });
            }
            
            // 连接到StreamClient的控制端口（如果未连接）
            if (!directChannel->isConnected()) {
                QUrl controlUrl(discoveredClient.controlUrl);
                if (controlUrl.isValid()) {
                    qInfo() << "[Console] Connecting WebSocket to" << clientId << "at" << controlUrl.toString();
                    directChannel->connectTo(controlUrl);
                    // 连接成功后，connected信号会触发sendDirectSubscribe
                } else {
                    qWarning() << "[Console] Invalid control URL for" << clientId << ":" << discoveredClient.controlUrl;
                }
            } else {
                // 连接已存在，立即发送订�?                qInfo() << "[Console] WebSocket already connected to" << clientId << "sending subscribe immediately";
                sendDirectSubscribe(clientId, ssrc, port);
            }
        }
    }
    
    // 兼容模式：如果WebSocket连接存在，也发送订阅（向后兼容�?    if (controlChannel_ && controlChannel_->isConnected()) {
    sendSubscribe(clientId, ssrc, port);
    }
    
    updateClientTreeItem(clientId);
    saveClientMetadata();
    updateWallHeaderStats();
}

void MainWindow::stopPreview(const QString& clientId) {
    auto itPlayer = activePlayers_.find(clientId);
    if (itPlayer == activePlayers_.end()) {
        removePreview(clientId);
        return;
    }

    StreamPlayer* player = itPlayer.value();
    
    // 停止录制（如果正在录制）
    // 注意：停止预览时不应该标记为手动停止，因为用户可能只是想停止预览，而不是停止录�?    // 只有在用户明确点�?停止录制"时才标记为手动停�?    if (player->isRecording()) {
        player->stopRecording();
    }
    
    // 完全直连方案：发送取消订阅消�?    auto itChannel = directControlChannels_.find(clientId);
    if (itChannel != directControlChannels_.end() && itChannel.value() && itChannel.value()->isConnected()) {
        sendDirectUnsubscribe(clientId, player->ssrc(), player->localPort());
    }
    
    // 兼容模式：如果WebSocket连接存在，也发送取消订阅（向后兼容�?    if (controlChannel_ && controlChannel_->isConnected()) {
    sendUnsubscribe(clientId, player->ssrc(), player->localPort());
    }
    
    // 清理WebSocket连接
    if (itChannel != directControlChannels_.end()) {
        if (itChannel.value()) {
            itChannel.value()->deleteLater();
        }
        directControlChannels_.erase(itChannel);
    }
    
    // 清理重连定时�?    auto itTimer = reconnectTimers_.find(clientId);
    if (itTimer != reconnectTimers_.end()) {
        itTimer.value()->stop();
        itTimer.value()->deleteLater();
        reconnectTimers_.erase(itTimer);
    }
    
    // 移除连接中标�?    connectingClients_.remove(clientId);
    
    player->stop();
    player->deleteLater();
    activePlayers_.erase(itPlayer);
    removePreview(clientId);
}

void MainWindow::removePreview(const QString& clientId) {
    auto itTile = activeTiles_.find(clientId);
    if (itTile == activeTiles_.end()) {
        return;
    }
    StreamTile* tile = itTile.value();
    activeTiles_.erase(itTile);
    tile->deleteLater();
    layoutOrder_.removeAll(clientId);
    if (activeFullscreen_ && !activeFullscreen_.isNull() && activeFullscreen_->clientId() == clientId) {
        activeFullscreen_->close();
    }
    tileStats_.remove(clientId);
    lastErrorTexts_.remove(clientId);
    if (!lastErrorTexts_.isEmpty()) {
        const auto it = lastErrorTexts_.constBegin();
        lastErrorMessage_ = QStringLiteral("%1: %2").arg(it.key(), it.value());
    } else {
        lastErrorMessage_.clear();
    }
    rebuildPreviewLayout();
    // updateStatusBarStats() 会在定时器中自动更新，不需要立即调�?    updateClientTreeItem(clientId);
}

void MainWindow::updateClientTreeItem(const QString& clientId) {
    auto* item = clientItems_.value(clientId, nullptr);
    if (!item) {
        return;
    }
    const ClientEntry entry = clientEntries_.value(clientId);
    QString displayName = entry.remark.isEmpty() ? clientId : QStringLiteral("%1 (%2)").arg(clientId, entry.remark);
    item->setText(0, displayName);
    item->setForeground(0, entry.online ? QBrush(QColor(0, 220, 0)) : QBrush(QColor(220, 0, 0)));
    item->setData(0, kRoleGroupName, entry.group.isEmpty() ? DefaultGroup() : entry.group);
}

void MainWindow::applyLayoutPreset(const QString& presetId) {
    QString normalized = presetId;
    if (normalized.isEmpty()) {
        normalized = QStringLiteral("1x4");
    }

    currentLayoutPreset_ = normalized;
    lastGridPreset_ = normalized;

    QStringList parts = normalized.split(QStringLiteral("x"), Qt::SkipEmptyParts);
    int rows = targetRows_;
    if (!parts.isEmpty()) {
        bool ok = false;
        const int parsedRows = parts.first().toInt(&ok);
        if (ok && parsedRows > 0) {
            rows = parsedRows;
        }
    }
    targetRows_ = qMax(1, rows);
    gridColumns_ = 4;

    statusBar()->showMessage(tr("布局切换�?%1 �?%2 �?).arg(gridColumns_).arg(targetRows_), 2000);
    rebuildPreviewLayout();
    updateLayoutActions();
}

void MainWindow::applyLayoutPresetFromAction() {
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action) {
        return;
    }
    const QString cmd = action->data().toString();
    if (cmd.startsWith(QStringLiteral("preset:"))) {
        const QString presetId = cmd.mid(QStringLiteral("preset:").size());
        applyLayoutPreset(presetId);
    }
}

void MainWindow::updateLayoutActions() {
    for (QAction* act : layoutPresetActions_) {
        if (!act) {
            continue;
        }
        const QString cmd = act->data().toString();
        if (cmd.startsWith(QStringLiteral("preset:"))) {
            const QString presetId = cmd.mid(QStringLiteral("preset:").size());
            act->setChecked(presetId == currentLayoutPreset_);
        }
    }
    if (wallFullscreenAction_) {
        wallFullscreenAction_->setChecked(wallFullscreen_);
    }
}

void MainWindow::updateTileStats(const QString& clientId, double fps, double mbps, const QString& errorText) {
    StreamTile* tile = activeTiles_.value(clientId, nullptr);
    if (!tile) {
        return;
    }

    tileStats_[clientId] = StreamStats{fps, mbps};
    tile->setStats(fps, mbps);

    if (!errorText.isEmpty()) {
        tile->setIndicator(StatusIndicator::Attention);
        tile->setErrorMessage(errorText);
        lastErrorTexts_[clientId] = errorText;
        lastErrorMessage_ = QStringLiteral("%1: %2").arg(clientId, errorText);
        statusBar()->showMessage(tr("%1 异常: %2").arg(clientId, errorText), 3000);
    } else {
        tile->setIndicator(StatusIndicator::Online);
        tile->setErrorMessage(QString());
        lastErrorTexts_.remove(clientId);
        if (!lastErrorTexts_.isEmpty()) {
            const auto it = lastErrorTexts_.constBegin();
            lastErrorMessage_ = QStringLiteral("%1: %2").arg(it.key(), it.value());
        } else {
            lastErrorMessage_.clear();
        }
    }

    if (activeFullscreen_ && !activeFullscreen_.isNull() && activeFullscreen_->clientId() == clientId) {
        QString viewerText = tr("帧率: %1 fps | 码率: %2 Mbps")
                                .arg(QString::number(fps, 'f', 1),
                                     QString::number(mbps, 'f', 2));
        if (!errorText.isEmpty()) {
            viewerText += tr(" | 异常: %1").arg(errorText);
        }
        activeFullscreen_->setStatsText(viewerText);
        if (!tile->currentFrame().isNull()) {
            activeFullscreen_->setFrame(tile->currentFrame());
        }
    }

    // updateStatusBarStats() 会在定时器中自动更新，不需要立即调�?    updateTileDisplayName(clientId);
}

void MainWindow::rebuildPreviewLayout() {
    // 优化：先移除所有布局�?    while (QLayoutItem* item = previewLayout_->takeAt(0)) {
        if (auto* w = item->widget()) {
            w->setParent(previewContainer_);
        }
        delete item;
    }

    const int spacing = previewLayout_->spacing();
    const QMargins margins = previewLayout_->contentsMargins();
    const bool dynamicColumns = wallFullscreen_;

    int tileWidth = StreamTile::kGridWidth;
    int tileHeight = StreamTile::kGridHeight;
    if (dynamicColumns) {
        int viewportWidth = previewScrollArea_ ? previewScrollArea_->viewport()->width()
                                               : previewContainer_->width();
        if (viewportWidth <= 0) {
            viewportWidth =
                gridColumns_ * StreamTile::kGridWidth + (gridColumns_ - 1) * spacing + margins.left() +
                margins.right();
        }
        const int effectiveWidth = viewportWidth - margins.left() - margins.right() -
                                   qMax(0, gridColumns_ - 1) * spacing;
        if (effectiveWidth > 0) {
            tileWidth = qMax(160, effectiveWidth / gridColumns_);
        }
        tileHeight = static_cast<int>(std::round(tileWidth * StreamTile::kAspectRatio));
    }

    const int usedWidth =
        tileWidth * gridColumns_ + qMax(0, gridColumns_ - 1) * spacing + margins.left() + margins.right();
    previewContainer_->setMinimumWidth(usedWidth);
    if (dynamicColumns) {
        previewContainer_->setMaximumWidth(QWIDGETSIZE_MAX);
    } else {
        previewContainer_->setMaximumWidth(usedWidth);
    }

    QVector<int> rowHeights;
    rowHeights.reserve(targetRows_ + 4);
    auto ensureRow = [&](int row) {
        if (row >= rowHeights.size()) {
            rowHeights.resize(row + 1, tileHeight);
        }
    };

    // 性能优化：预分配容量，减少内存重新分�?    int index = 0;
    QSet<QString> placed;
    placed.reserve(activeTiles_.size());

    // 优化：批量处理已排序的客户端
    for (const QString& clientId : layoutOrder_) {
        StreamTile* tile = activeTiles_.value(clientId, nullptr);
        if (!tile) {
            continue;
        }
        placed.insert(clientId);
        tile->applyGridSizing(true, tileWidth);
        tile->setVisible(true);
        const int row = index / gridColumns_;
        const int col = index % gridColumns_;
        ensureRow(row);
        rowHeights[row] = qMax(rowHeights[row], tile->minimumHeight());
        previewLayout_->addWidget(tile, row, col);
        ++index;
    }

    // 优化：批量查找缺失的客户端（减少查找次数�?    QVector<QString> missing;
    missing.reserve(activeTiles_.size() - placed.size());
    for (auto it = activeTiles_.cbegin(); it != activeTiles_.cend(); ++it) {
        if (!placed.contains(it.key())) {
            missing.append(it.key());
        }
    }

    // 批量添加缺失的客户端到布局顺序
    for (const QString& clientId : missing) {
        layoutOrder_.append(clientId);
        StreamTile* tile = activeTiles_.value(clientId, nullptr);
        if (!tile) {
            continue;
        }
        tile->applyGridSizing(true, tileWidth);
        tile->setVisible(true);
        const int row = index / gridColumns_;
        const int col = index % gridColumns_;
        ensureRow(row);
        rowHeights[row] = qMax(rowHeights[row], tile->minimumHeight());
        previewLayout_->addWidget(tile, row, col);
        ++index;
    }

    const int rowsUsed = (index + gridColumns_ - 1) / gridColumns_;
    const int rowsDisplayed = qMax(targetRows_, qMax(1, rowsUsed));

    const int requiredSlots = rowsDisplayed * gridColumns_;
    int placeholderCursor = 0;
    for (PlaceholderTile* tile : placeholderTiles_) {
        tile->hide();
    }
    while (placeholderTiles_.size() < requiredSlots) {
        auto* placeholder = new PlaceholderTile(previewContainer_);
        placeholderTiles_.append(placeholder);
    }
    for (int slot = index; slot < requiredSlots; ++slot) {
        PlaceholderTile* placeholder = placeholderTiles_.at(placeholderCursor++);
        placeholder->setParent(previewContainer_);
        placeholder->setTileSize(tileWidth, tileHeight);
        placeholder->show();
        const int row = slot / gridColumns_;
        const int col = slot % gridColumns_;
        ensureRow(row);
        rowHeights[row] = qMax(rowHeights[row], placeholder->minimumHeight());
        previewLayout_->addWidget(placeholder, row, col);
    }

    if (rowHeights.size() < rowsDisplayed) {
        rowHeights.resize(rowsDisplayed, tileHeight);
    }

    int totalHeight = margins.top() + margins.bottom();
    for (int i = 0; i < rowsDisplayed; ++i) {
        totalHeight += qMax(rowHeights.value(i, tileHeight), tileHeight);
    }
    if (rowsDisplayed > 0) {
        totalHeight += qMax(0, rowsDisplayed - 1) * spacing;
    }

    previewContainer_->setMinimumHeight(totalHeight);
    previewContainer_->setMaximumHeight(QWIDGETSIZE_MAX);

    previewContainer_->updateGeometry();
    updateTileDragEnabled();
}

void MainWindow::sendSubscribe(const QString& clientId, quint32 ssrc, quint16 port) {
    if (!controlChannel_) {
        return;
    }
    qInfo() << "[Console] Subscribe request" << clientId << "ssrc" << ssrc << "port" << port;
    QJsonObject obj{
        {QStringLiteral("action"), QStringLiteral("subscribe")},
        {QStringLiteral("client_id"), clientId},
        {QStringLiteral("ssrc"), static_cast<qint64>(ssrc)},
        {QStringLiteral("port"), static_cast<int>(port)},
        {QStringLiteral("host"), QStringLiteral("127.0.0.1")}
    };
    controlChannel_->sendText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void MainWindow::sendUnsubscribe(const QString& clientId, quint32 ssrc, quint16 port) {
    // 兼容模式：通过StreamServer取消订阅（向后兼容）
    if (!controlChannel_) {
        return;
    }
    QJsonObject obj{
        {QStringLiteral("action"), QStringLiteral("unsubscribe")},
        {QStringLiteral("client_id"), clientId},
        {QStringLiteral("ssrc"), static_cast<qint64>(ssrc)},
        {QStringLiteral("port"), static_cast<int>(port)},
        {QStringLiteral("host"), QStringLiteral("127.0.0.1")}
    };
    controlChannel_->sendText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void MainWindow::sendDirectSubscribe(const QString& clientId, quint32 ssrc, quint16 port) {
    // 完全直连方案：直接发送订阅消息到StreamClient
    auto itChannel = directControlChannels_.find(clientId);
    if (itChannel == directControlChannels_.end() || !itChannel.value() || !itChannel.value()->isConnected()) {
        qWarning() << "[Console] Direct WebSocket not connected for" << clientId;
        return;
    }
    
    // 获取本地IP地址
    QString localIP = QStringLiteral("127.0.0.1");
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
                localIP = ip.toString();
                break;
            }
        }
        if (localIP != QStringLiteral("127.0.0.1")) {
            break;
        }
    }
    
    qInfo() << "[Console] Direct subscribe request to" << clientId << "ssrc" << ssrc << "port" << port << "from" << localIP;
    QJsonObject obj{
        {QStringLiteral("action"), QStringLiteral("subscribe")},
        {QStringLiteral("client_id"), clientId},
        {QStringLiteral("ssrc"), static_cast<qint64>(ssrc)},
        {QStringLiteral("port"), static_cast<int>(port)},
        {QStringLiteral("host"), localIP}
    };
    itChannel.value()->sendText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void MainWindow::sendDirectUnsubscribe(const QString& clientId, quint32 ssrc, quint16 port) {
    // 完全直连方案：直接发送取消订阅消息到StreamClient
    auto itChannel = directControlChannels_.find(clientId);
    if (itChannel == directControlChannels_.end() || !itChannel.value() || !itChannel.value()->isConnected()) {
        return;
    }
    
    QString localIP = QStringLiteral("127.0.0.1");
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
                localIP = ip.toString();
                break;
            }
        }
        if (localIP != QStringLiteral("127.0.0.1")) {
            break;
        }
    }
    
    qInfo() << "[Console] Direct unsubscribe request to" << clientId;
    QJsonObject obj{
        {QStringLiteral("action"), QStringLiteral("unsubscribe")},
        {QStringLiteral("client_id"), clientId},
        {QStringLiteral("ssrc"), static_cast<qint64>(ssrc)},
        {QStringLiteral("port"), static_cast<int>(port)},
        {QStringLiteral("host"), localIP}
    };
    itChannel.value()->sendText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void MainWindow::toggleWallFullscreen() {
    setWallFullscreen(!wallFullscreen_);
}

void MainWindow::setWallFullscreen(bool enable) {
    if (wallFullscreen_ == enable) {
        return;
    }
    wallFullscreen_ = enable;
    auto* toolBar = findChild<QToolBar*>(QStringLiteral("main_toolbar"));
    if (enable) {
        wallFullscreenReturnPreset_ = currentLayoutPreset_;
        wallFullscreenReturnTargetRows_ = targetRows_;
        if (clientTree_) {
            clientTreeVisibleBeforeWall_ = clientTree_->isVisible();
            clientTree_->setVisible(false);
        } else {
            clientTreeVisibleBeforeWall_ = false;
        }
        previewLayout_->setContentsMargins(0, 0, 0, 0);
        previewLayout_->setSpacing(0);

        if (currentLayoutPreset_ != QStringLiteral("1x4") || targetRows_ != 1) {
            applyLayoutPreset(QStringLiteral("1x4"));
            schedulePreviewRelayout();
        } else {
            targetRows_ = 1;
            rebuildPreviewLayout();
            updateLayoutActions();
        }

        normalGeometry_ = geometry();
        normalWindowState_ = windowState();
        showFullScreen();
        // 工具栏始终隐藏，不显�?        statusBar()->showMessage(tr("监控墙已全屏"), 2000);
    } else {
        if (normalWindowState_.testFlag(Qt::WindowMaximized)) {
            showNormal();
            showMaximized();
        } else if (normalWindowState_.testFlag(Qt::WindowFullScreen)) {
            showFullScreen();
        } else {
            showNormal();
            if (normalGeometry_.isValid()) {
                setGeometry(normalGeometry_);
            }
        }
        // 工具栏始终隐藏，不显�?        previewLayout_->setContentsMargins(previewMarginsNormal_);
        previewLayout_->setSpacing(previewSpacingNormal_);
        statusBar()->showMessage(tr("监控墙已退出全�?), 2000);

        if (clientTree_) {
            clientTree_->setVisible(clientTreeVisibleBeforeWall_);
        }

        if (!wallFullscreenReturnPreset_.isEmpty()) {
            const QString restorePreset = wallFullscreenReturnPreset_;
            wallFullscreenReturnPreset_.clear();
            if (restorePreset != currentLayoutPreset_) {
                applyLayoutPreset(restorePreset);
                schedulePreviewRelayout();
            } else {
                targetRows_ = wallFullscreenReturnTargetRows_;
                rebuildPreviewLayout();
                updateLayoutActions();
            }
        } else {
            rebuildPreviewLayout();
        }
        schedulePreviewRelayout();
    }
    updateLayoutActions();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (wallFullscreen_) {
            setWallFullscreen(false);
            event->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (previewScrollArea_ && watched == previewScrollArea_->viewport()) {
        if (event->type() == QEvent::Wheel) {
            auto* wheelEvent = static_cast<QWheelEvent*>(event);
            if (auto* vScroll = previewScrollArea_->verticalScrollBar()) {
                const int delta = wheelEvent->angleDelta().y();
                if (delta != 0) {
                    vScroll->setValue(vScroll->value() - delta);
                    return true;
                }
            }
        } else if (event->type() == QEvent::Resize) {
            if (wallFullscreen_) {
                schedulePreviewRelayout();
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::handlePreviewContextMenu(const QPoint& pos) {
    if (!previewContainer_) {
        return;
    }
    QMenu menu(this);

    QAction* wallAction = menu.addAction(wallFullscreen_ ? tr("退出监控墙全屏")
                                                         : tr("进入监控墙全�?));

    menu.addSeparator();
    QMenu* layoutMenu = menu.addMenu(tr("布局预设"));
    appendLayoutActions(layoutMenu);

    QAction* refreshAction = menu.addAction(tr("刷新监控�?));

    QAction* chosen = menu.exec(previewContainer_->mapToGlobal(pos));
    if (!chosen) {
        return;
    }

    if (chosen == wallAction) {
        setWallFullscreen(!wallFullscreen_);
        return;
    }
    if (chosen == refreshAction) {
        requestClientList();
        return;
    }
}

void MainWindow::openFullscreenView(const QString& clientId) {
    if (activeFullscreen_ && !activeFullscreen_.isNull()) {
        if (activeFullscreen_->clientId() == clientId) {
            qInfo() << "[Fullscreen] Viewer already open for" << clientId;
            activeFullscreen_->raise();
            activeFullscreen_->activateWindow();
            return;
        }
        qInfo() << "[Fullscreen] Closing previous viewer for" << activeFullscreen_->clientId();
        activeFullscreen_->close();
        activeFullscreen_.clear();
    }
    for (const auto& connection : activeFullscreenConnections_) {
        QObject::disconnect(connection);
    }
    activeFullscreenConnections_.clear();

    // UDP 模式：仅检�?Tile 是否存在
    StreamTile* tile = activeTiles_.value(clientId, nullptr);
    if (!tile) {
        statusBar()->showMessage(tr("客户�?%1 未连�?).arg(clientId), 2000);
        return;
    }

    qInfo() << "[Fullscreen] Opening viewer for" << clientId;

    auto* viewer = new FullscreenView(clientId);  // top-level window
    viewer->setWindowTitle(tr("全屏 - %1").arg(clientId));
    if (!tile->currentFrame().isNull()) {
        viewer->setFrame(tile->currentFrame());
    }
    
    // 连接关闭信号
    connect(viewer, &FullscreenView::closed, this, [this]() {
        activeFullscreen_ = nullptr;
        activeFullscreenConnections_.clear();
    });
    
    // 保存全屏窗口引用以便实时更新
    activeFullscreen_ = viewer;

    const auto stats = tileStats_.value(clientId, StreamStats{});
    QString text = viewer->tr("帧率: %1 fps | 码率: %2 Mbps")
                         .arg(QString::number(stats.fps, 'f', 1),
                              QString::number(stats.mbps, 'f', 2));
    if (const QString error = lastErrorTexts_.value(clientId); !error.isEmpty()) {
        text += viewer->tr(" | 异常: %1").arg(error);
    }
    viewer->setStatsText(text);

    QPointer<FullscreenView> viewerPtr(viewer);
    activeFullscreen_ = viewerPtr;

    activeFullscreenConnections_.append(connect(viewer, &FullscreenView::viewerClosed, this, [this, clientId]() {
        qInfo() << "[Fullscreen] Closed viewer for" << clientId;
        for (const auto& connection : activeFullscreenConnections_) {
            QObject::disconnect(connection);
        }
        activeFullscreenConnections_.clear();
        activeFullscreen_.clear();
        statusBar()->showMessage(tr("已退�?%1 全屏预览").arg(clientId), 2000);
    }));

    activeFullscreenConnections_.append(connect(viewer,
                                                 &FullscreenView::exitRequested,
                                                 this,
                                                 [viewerPtr](const QString&) {
                                                     if (!viewerPtr.isNull()) {
                                                         viewerPtr->close();
                                                     }
                                                 }));

    activeFullscreenConnections_.append(connect(player,
                                                 &StreamPlayer::frameUpdated,
                                                 viewer,
                                                 [viewerPtr](const QString& id, const QImage& frame) {
                                                     if (viewerPtr.isNull()) {
                                                         return;
                                                     }
                                                     if (id == viewerPtr->clientId()) {
                                                         viewerPtr->setFrame(frame);
                                                     }
                                                 },
                                                 Qt::QueuedConnection));

    activeFullscreenConnections_.append(connect(player,
                                                 &StreamPlayer::statsUpdated,
                                                 viewer,
                                                 [viewerPtr](const QString& id, double fps, double mbps, const QString& error) {
                                                     if (viewerPtr.isNull()) {
                                                         return;
                                                     }
                                                     if (id == viewerPtr->clientId()) {
                                                         QString info = viewerPtr->tr("帧率: %1 fps | 码率: %2 Mbps")
                                                                             .arg(QString::number(fps, 'f', 1),
                                                                                  QString::number(mbps, 'f', 2));
                                                         if (!error.isEmpty()) {
                                                             info += viewerPtr->tr(" | 异常: %1").arg(error);
                                                         }
                                                         viewerPtr->setStatsText(info);
                                                     }
                                                 },
                                                 Qt::QueuedConnection));

    QTimer::singleShot(0, viewer, [viewerPtr, clientId]() {
        if (viewerPtr.isNull()) {
            return;
        }
        qInfo() << "[Fullscreen] Showing viewer for" << clientId;
        viewerPtr->show();
        viewerPtr->setWindowState(viewerPtr->windowState() | Qt::WindowFullScreen);
        viewerPtr->raise();
        viewerPtr->activateWindow();
    });
    statusBar()->showMessage(tr("%1 全屏预览，可�?ESC 或双击退�?).arg(clientId), 2000);
}

void MainWindow::handleClearAllData() {
    QMessageBox::StandardButton confirmBtn = QMessageBox::question(
        this,
        tr("确认清除所有数�?),
        tr("此操作将清除所有记录的数据，包括：\n\n"
           "�?活动日志\n"
           "�?截图记录\n"
           "�?软件使用统计\n"
           "�?敏感词预警\n"
           "�?窗口变更截图配置\n\n"
           "此操作不可恢复！\n\n"
           "确定要继续吗�?),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    
    if (confirmBtn != QMessageBox::Yes) {
        return;
    }
    
    // 再次确认
    QMessageBox::StandardButton finalConfirm = QMessageBox::warning(
        this,
        tr("最后确�?),
        tr("⚠️ 警告：此操作将永久删除所有数据！\n\n"
           "请再次确认是否继续？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    
    if (finalConfirm != QMessageBox::Yes) {
        return;
    }
    
    // 发送清除请求到CommandController
    if (!restApiManager_) {
        QMessageBox::warning(this, tr("错误"), tr("网络管理器未初始�?));
        return;
    }
    
    const QString baseUrl = QStringLiteral("http://127.0.0.1:8080");
    const QUrl url(QStringLiteral("%1/api/database/clear-all").arg(baseUrl));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(30000);  // 30秒超�?    
    QJsonObject payload;
    payload.insert(QStringLiteral("confirm"), true);
    
    QNetworkReply* networkReply = restApiManager_->post(request, QJsonDocument(payload).toJson());
    
    // 显示进度提示
    statusBar()->showMessage(tr("正在清除所有数据，请稍�?.."), 0);
    
    connect(networkReply, &QNetworkReply::finished, this, [this, networkReply]() {
        statusBar()->clearMessage();
        
        if (networkReply->error() != QNetworkReply::NoError) {
            QMessageBox::critical(this, tr("错误"), 
                tr("清除数据失败�?1").arg(networkReply->errorString()));
            networkReply->deleteLater();
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(networkReply->readAll());
        QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();
        
        networkReply->deleteLater();
        
        const QString status = obj.value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("ok")) {
            const int deletedCount = obj.value(QStringLiteral("deleted_records")).toInt();
            QMessageBox::information(this, tr("成功"), 
                tr("所有数据已清除！\n\n已删�?%1 条记�?).arg(deletedCount));
            
            // 刷新客户端列表和所有打开的详情对话框
            requestClientList();
            
            // 关闭所有打开的详情对话框
            for (ClientDetailsDialog* dialog : findChildren<ClientDetailsDialog*>()) {
                dialog->refreshAllData();
            }
        } else {
            const QString message = obj.value(QStringLiteral("message")).toString();
            QMessageBox::warning(this, tr("失败"), 
                tr("清除数据失败�?1").arg(message.isEmpty() ? tr("未知错误") : message));
        }
    });
}

void MainWindow::handleSaveTimeSettings() {
    // 创建保存时间设置对话�?
    QDialog dialog(this);
    dialog.setWindowTitle(tr("视频流保存设�?));
    dialog.setMinimumWidth(500);
    dialog.setStyleSheet(QStringLiteral(
        "QDialog { background-color: #0f172a; }"
        "QLabel { color: #e2e8f0; }"
        "QLineEdit { background-color: #1e293b; color: #e2e8f0; border: 1px solid #334155; padding: 4px; }"
        "QPushButton { background-color: #3b82f6; color: white; border: none; padding: 6px 16px; }"
        "QPushButton:hover { background-color: #2563eb; }"
        "QSpinBox { background-color: #1e293b; color: #e2e8f0; border: 1px solid #334155; padding: 4px; }"
        "QGroupBox { color: #e2e8f0; border: 1px solid #334155; padding-top: 10px; margin-top: 10px; }"));
    
    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(12);
    
    // 保存时长设置
    auto* durationGroup = new QGroupBox(tr("保存时长设置"));
    auto* durationLayout = new QHBoxLayout();
    durationLayout->addWidget(new QLabel(tr("保存时长（小时）")));
    auto* durationSpinBox = new QSpinBox();
    durationSpinBox->setRange(1, 720);  // 1小时�?0�?    durationSpinBox->setValue(videoSaveDurationHours_);  // 使用当前设置的�?    durationSpinBox->setSuffix(tr(" 小时"));
    durationLayout->addWidget(durationSpinBox);
    durationLayout->addStretch();
    durationGroup->setLayout(durationLayout);
    layout->addWidget(durationGroup);
    
    // 保存位置设置
    auto* pathGroup = new QGroupBox(tr("保存位置设置"));
    auto* pathLayout = new QVBoxLayout();
    auto* pathInputLayout = new QHBoxLayout();
    auto* pathLineEdit = new QLineEdit();
    pathLineEdit->setPlaceholderText(tr("选择保存目录..."));
    // 使用当前设置的值，如果为空则使用默认路�?    const QString defaultPath = videoSavePath_.isEmpty() 
        ? (QCoreApplication::applicationDirPath() + QStringLiteral("/recordings"))
        : videoSavePath_;
    pathLineEdit->setText(defaultPath);
    pathInputLayout->addWidget(new QLabel(tr("保存目录")));
    pathInputLayout->addWidget(pathLineEdit, 1);
    auto* browseButton = new QPushButton(tr("浏览..."));
    pathInputLayout->addWidget(browseButton);
    pathLayout->addLayout(pathInputLayout);
    
    auto* infoLabel = new QLabel(tr("文件名格式：{主机名}_{时间戳}.mp4"));
    infoLabel->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px;"));
    pathLayout->addWidget(infoLabel);
    pathGroup->setLayout(pathLayout);
    layout->addWidget(pathGroup);
    
    // 按钮
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    auto* okButton = new QPushButton(tr("确定"));
    auto* cancelButton = new QPushButton(tr("取消"));
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);
    
    connect(browseButton, &QPushButton::clicked, [pathLineEdit]() {
        QString dir = QFileDialog::getExistingDirectory(nullptr, tr("选择保存目录"), pathLineEdit->text());
        if (!dir.isEmpty()) {
            pathLineEdit->setText(dir);
        }
    });
    
    connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    
    if (dialog.exec() == QDialog::Accepted) {
        const int durationHours = durationSpinBox->value();
        const QString savePath = pathLineEdit->text().trimmed();
        
        if (savePath.isEmpty()) {
            QMessageBox::warning(this, tr("错误"), tr("保存目录不能为空"));
            return;
        }
        
        // 保存配置
        videoSaveDurationHours_ = durationHours;
        videoSavePath_ = savePath;
        
        // 确保目录存在
        QDir().mkpath(savePath);
        QMessageBox::information(this, tr("成功"), 
            tr("视频流保存设置已保存！\n\n"
               "保存时长�?1 小时\n"
               "保存位置�?2\n\n"
               "文件名将以主机名命名").arg(durationHours).arg(savePath));
        
        // 如果已有活动的播放器，更新它们的录制设置（仅对未被手动停止的客户端）
        for (auto it = activePlayers_.begin(); it != activePlayers_.end(); ++it) {
            StreamPlayer* player = it.value();
            const QString& clientId = it.key();
            // 如果该客户端未被手动停止录制，则自动开始录�?            if (player && !recordingDisabledClients_.contains(clientId)) {
                if (!player->isRecording()) {
                    QString hostname = clientId;
                    auto entryIt = clientEntries_.find(clientId);
                    if (entryIt != clientEntries_.end() && !entryIt->remark.isEmpty()) {
                        hostname = entryIt->remark;
                    }
                    player->startRecording(hostname, savePath, durationHours);
                }
            }
        }
    }
}

void MainWindow::handleViewVideoRecords(const QString& clientId) {
    if (videoSavePath_.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("视频保存路径未配置，请先在\"保存时间\"设置中配置保存路�?));
        return;
    }
    
    QDir saveDir(videoSavePath_);
    if (!saveDir.exists()) {
        QMessageBox::information(this, tr("提示"), tr("视频保存目录不存在：%1").arg(videoSavePath_));
        return;
    }
    
    // 获取客户端的主机�?    QString hostname = clientId;
    auto entryIt = clientEntries_.find(clientId);
    if (entryIt != clientEntries_.end() && !entryIt->remark.isEmpty()) {
        hostname = entryIt->remark;
    }
    
    // 查找该客户端的视频文件（同时查找clientId和hostname命名的文件）
    QStringList nameFilters;
    nameFilters << QStringLiteral("%1_*.mp4").arg(clientId);
    if (!hostname.isEmpty() && hostname != clientId) {
        nameFilters << QStringLiteral("%1_*.mp4").arg(hostname);
    }
    
    QFileInfoList videoFiles = saveDir.entryInfoList(
        nameFilters,
        QDir::Files,
        QDir::Time | QDir::Reversed  // 最新的在前
    );
    
    // 同时查找临时目录（可能还在转换中�?    QStringList tempDirFilters;
    tempDirFilters << QStringLiteral("temp_%1_*").arg(clientId);
    if (!hostname.isEmpty() && hostname != clientId) {
        tempDirFilters << QStringLiteral("temp_%1_*").arg(hostname);
    }
    
    QFileInfoList tempDirs = saveDir.entryInfoList(
        tempDirFilters,
        QDir::Dirs | QDir::NoDotAndDotDot
    );
    
    qInfo() << "[Console] Searching video files for clientId=" << clientId << "hostname=" << hostname;
    qInfo() << "[Console] Search path:" << saveDir.absolutePath();
    qInfo() << "[Console] Name filters:" << nameFilters;
    qInfo() << "[Console] Temp dir filters:" << tempDirFilters;
    qInfo() << "[Console] Found" << videoFiles.size() << "MP4 files and" << tempDirs.size() << "temp directories";
    
    // 调试：列出所有文�?    QFileInfoList allFiles = saveDir.entryInfoList(QDir::Files);
    qInfo() << "[Console] All files in directory:" << allFiles.size();
    for (const QFileInfo& info : allFiles) {
        qInfo() << "[Console]   -" << info.fileName();
    }
    
    // 创建对话框显示视频列�?    QDialog dialog(this);
    dialog.setWindowTitle(tr("视频记录 - %1").arg(clientId));
    dialog.setMinimumSize(800, 500);
    dialog.setStyleSheet(QStringLiteral(
        "QDialog { background-color: #0f172a; }"
        "QLabel { color: #e2e8f0; }"
        "QTableWidget { background-color: #0f172a; color: #e2e8f0; border: 1px solid #1e293b; gridline-color: #1e293b; }"
        "QTableWidget::item:selected { background-color: #3b82f6; color: white; }"
        "QTableWidget::item:hover { background-color: #1e293b; }"
        "QHeaderView::section { background-color: #1e293b; color: #e2e8f0; padding: 4px; border: none; }"
        "QPushButton { background-color: #3b82f6; color: white; border: none; padding: 6px 16px; }"
        "QPushButton:hover { background-color: #2563eb; }"));
    
    auto* layout = new QVBoxLayout(&dialog);
    
    // 计算总文件数（包括临时目录）
    int totalItems = videoFiles.size() + tempDirs.size();
    auto* infoLabel = new QLabel(tr("共找�?%1 个视频文件（%2 个已完成�?3 个转换中�?)
        .arg(totalItems).arg(videoFiles.size()).arg(tempDirs.size()));
    infoLabel->setStyleSheet(QStringLiteral("color: #e2e8f0; padding: 8px;"));
    layout->addWidget(infoLabel);
    
    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({tr("文件�?状�?), tr("大小"), tr("创建时间"), tr("操作")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setRowCount(totalItems);
    
    int row = 0;
    
    // 先显示已完成的MP4文件
    for (int i = 0; i < videoFiles.size(); ++i) {
        const QFileInfo& info = videoFiles.at(i);
        
        // 文件�?        table->setItem(row, 0, new QTableWidgetItem(info.fileName()));
        
        // 文件大小
        qint64 sizeBytes = info.size();
        QString sizeStr;
        if (sizeBytes < 1024) {
            sizeStr = QStringLiteral("%1 B").arg(sizeBytes);
        } else if (sizeBytes < 1024 * 1024) {
            sizeStr = QStringLiteral("%1 KB").arg(sizeBytes / 1024.0, 0, 'f', 2);
        } else {
            sizeStr = QStringLiteral("%1 MB").arg(sizeBytes / (1024.0 * 1024.0), 0, 'f', 2);
        }
        table->setItem(row, 1, new QTableWidgetItem(sizeStr));
        
        // 创建时间（北京时间）
        const QDateTime beijingTime = info.lastModified().toTimeZone(QTimeZone("Asia/Shanghai"));
        table->setItem(row, 2, new QTableWidgetItem(beijingTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        
        // 操作按钮
        auto* buttonContainer = new QWidget();
        auto* buttonLayout = new QHBoxLayout(buttonContainer);
        buttonLayout->setContentsMargins(2, 2, 2, 2);
        buttonLayout->setSpacing(4);
        
        auto* openButton = new QPushButton(tr("打开"));
        openButton->setProperty("filePath", info.absoluteFilePath());
        connect(openButton, &QPushButton::clicked, [this, info]() {
            openVideoPlayer(info.absoluteFilePath());
        });
        buttonLayout->addWidget(openButton);
        
        auto* systemButton = new QPushButton(tr("系统播放�?));
        systemButton->setProperty("filePath", info.absoluteFilePath());
        connect(systemButton, &QPushButton::clicked, [info]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
        });
        buttonLayout->addWidget(systemButton);
        
        table->setCellWidget(row, 3, buttonContainer);
        
        // 设置行高
        table->setRowHeight(row, 30);
        row++;
    }
    
    // 再显示临时目录（转换中）
    for (int i = 0; i < tempDirs.size(); ++i) {
        const QFileInfo& dirInfo = tempDirs.at(i);
        QDir tempDir(dirInfo.absoluteFilePath());
        
        // 统计临时目录中的JPEG帧数
        QFileInfoList jpegFrames = tempDir.entryInfoList(
            QStringList() << "frame_*.jpg",
            QDir::Files
        );
        
        // 状态显�?        QString statusText = QStringLiteral("%1 [转换中]").arg(dirInfo.fileName());
        if (jpegFrames.size() > 0) {
            statusText += QStringLiteral(" (%1 �?").arg(jpegFrames.size());
        }
        table->setItem(row, 0, new QTableWidgetItem(statusText));
        
        // 计算临时目录总大�?        qint64 totalSize = 0;
        for (const QFileInfo& frame : jpegFrames) {
            totalSize += frame.size();
        }
        QString sizeStr;
        if (totalSize < 1024) {
            sizeStr = QStringLiteral("%1 B").arg(totalSize);
        } else if (totalSize < 1024 * 1024) {
            sizeStr = QStringLiteral("%1 KB").arg(totalSize / 1024.0, 0, 'f', 2);
        } else {
            sizeStr = QStringLiteral("%1 MB").arg(totalSize / (1024.0 * 1024.0), 0, 'f', 2);
        }
        table->setItem(row, 1, new QTableWidgetItem(sizeStr));
        
        // 创建时间（北京时间）
        const QDateTime beijingTime = dirInfo.lastModified().toTimeZone(QTimeZone("Asia/Shanghai"));
        table->setItem(row, 2, new QTableWidgetItem(beijingTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        
        // 操作按钮（打开临时目录�?        auto* openButton = new QPushButton(tr("查看"));
        openButton->setProperty("dirPath", dirInfo.absoluteFilePath());
        connect(openButton, &QPushButton::clicked, [dirInfo]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(dirInfo.absoluteFilePath()));
        });
        table->setCellWidget(row, 3, openButton);
        
        // 设置行高
        table->setRowHeight(row, 30);
        row++;
    }
    
    // 调整列宽
    table->setColumnWidth(0, 300);
    table->setColumnWidth(1, 100);
    table->setColumnWidth(2, 180);
    table->setColumnWidth(3, 80);
    
    layout->addWidget(table, 1);
    
    // 按钮
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    auto* refreshButton = new QPushButton(tr("刷新"));
    auto* closeButton = new QPushButton(tr("关闭"));
    buttonLayout->addWidget(refreshButton);
    buttonLayout->addWidget(closeButton);
    layout->addLayout(buttonLayout);
    
    connect(refreshButton, &QPushButton::clicked, [&dialog, &saveDir, clientId, hostname, table]() {
        // 重新查找文件
        QStringList nameFilters;
        nameFilters << QStringLiteral("%1_*.mp4").arg(clientId);
        if (!hostname.isEmpty() && hostname != clientId) {
            nameFilters << QStringLiteral("%1_*.mp4").arg(hostname);
        }
        QFileInfoList newVideoFiles = saveDir.entryInfoList(nameFilters, QDir::Files, QDir::Time | QDir::Reversed);
        QFileInfoList newTempDirs = saveDir.entryInfoList(
            QStringList() << QStringLiteral("temp_%1_*").arg(clientId) 
                          << QStringLiteral("temp_%1_*").arg(hostname.isEmpty() ? clientId : hostname),
            QDir::Dirs | QDir::NoDotAndDotDot
        );
        
        // 简化处理：关闭对话框，用户需要重新打开来查看更新后的列�?        dialog.accept();
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    
    dialog.exec();
}

// FFmpeg视频解码器类（用于嵌入式播放�?class FFmpegVideoDecoder : public QObject {
    Q_OBJECT
public:
    explicit FFmpegVideoDecoder(QObject* parent = nullptr) : QObject(parent) {
        formatCtx_ = nullptr;
        codecCtx_ = nullptr;
        frame_ = nullptr;
        frameRGB_ = nullptr;
        swsCtx_ = nullptr;
        videoStreamIndex_ = -1;
        duration_ = 0;
        width_ = 0;
        height_ = 0;
        buffer_ = nullptr;
    }
    
    ~FFmpegVideoDecoder() override {
        cleanup();
    }
    
    bool open(const QString& filePath) {
        cleanup();
        
        // 打开视频文件
        if (avformat_open_input(&formatCtx_, filePath.toUtf8().constData(), nullptr, nullptr) != 0) {
            qWarning() << "[FFmpeg] Failed to open input file:" << filePath;
            return false;
        }
        
        // 查找流信�?        if (avformat_find_stream_info(formatCtx_, nullptr) < 0) {
            qWarning() << "[FFmpeg] Failed to find stream info";
            cleanup();
            return false;
        }
        
        // 查找视频�?        videoStreamIndex_ = -1;
        for (unsigned int i = 0; i < formatCtx_->nb_streams; i++) {
            if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStreamIndex_ = static_cast<int>(i);
                break;
            }
        }
        
        if (videoStreamIndex_ == -1) {
            qWarning() << "[FFmpeg] No video stream found";
            cleanup();
            return false;
        }
        
        // 获取解码器参�?        AVCodecParameters* codecPar = formatCtx_->streams[videoStreamIndex_]->codecpar;
        
        // 查找解码�?        const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
        if (!codec) {
            qWarning() << "[FFmpeg] Codec not found";
            cleanup();
            return false;
        }
        
        // 创建解码器上下文
        codecCtx_ = avcodec_alloc_context3(codec);
        if (!codecCtx_) {
            qWarning() << "[FFmpeg] Failed to allocate codec context";
            cleanup();
            return false;
        }
        
        // 复制解码器参�?        if (avcodec_parameters_to_context(codecCtx_, codecPar) < 0) {
            qWarning() << "[FFmpeg] Failed to copy codec parameters";
            cleanup();
            return false;
        }
        
        // 打开解码�?        if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
            qWarning() << "[FFmpeg] Failed to open codec";
            cleanup();
            return false;
        }
        
        width_ = codecCtx_->width;
        height_ = codecCtx_->height;
        duration_ = formatCtx_->duration / AV_TIME_BASE * 1000;  // 转换为毫�?        
        // 获取视频帧率
        AVRational fps = formatCtx_->streams[videoStreamIndex_]->r_frame_rate;
        if (fps.num == 0 || fps.den == 0) {
            // 如果r_frame_rate无效，尝试avg_frame_rate
            fps = formatCtx_->streams[videoStreamIndex_]->avg_frame_rate;
        }
        if (fps.num > 0 && fps.den > 0) {
            fps_ = static_cast<double>(fps.num) / fps.den;
        } else {
            // 默认30fps
            fps_ = 30.0;
        }
        qInfo() << "[FFmpeg] Video FPS:" << fps_;
        
        // 分配�?        frame_ = av_frame_alloc();
        frameRGB_ = av_frame_alloc();
        if (!frame_ || !frameRGB_) {
            qWarning() << "[FFmpeg] Failed to allocate frames";
            cleanup();
            return false;
        }
        
        // 分配RGB缓冲�?        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width_, height_, 1);
        buffer_ = static_cast<uint8_t*>(av_malloc(numBytes * sizeof(uint8_t)));
        av_image_fill_arrays(frameRGB_->data, frameRGB_->linesize, buffer_, AV_PIX_FMT_RGB24, width_, height_, 1);
        
        // 创建SWS上下文用于格式转换（设置正确的range以避免警告）
        swsCtx_ = sws_getContext(width_, height_, codecCtx_->pix_fmt,
                                 width_, height_, AV_PIX_FMT_RGB24,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
        
        // 设置颜色空间和range以避免deprecated警告
        if (swsCtx_) {
            const int* coeffs = sws_getCoefficients(SWS_CS_ITU709);
            sws_setColorspaceDetails(swsCtx_, coeffs, codecCtx_->color_range == AVCOL_RANGE_JPEG ? 1 : 0,
                                     coeffs, codecCtx_->color_range == AVCOL_RANGE_JPEG ? 1 : 0,
                                     0, 1 << 16, 1 << 16);
        }
        
        if (!swsCtx_) {
            qWarning() << "[FFmpeg] Failed to create SWS context";
            cleanup();
            return false;
        }
        
        qInfo() << "[FFmpeg] Video opened successfully:" << width_ << "x" << height_ << "duration:" << duration_ << "ms";
        return true;
    }
    
    bool seekTo(qint64 positionMs) {
        if (!formatCtx_ || videoStreamIndex_ < 0) {
            return false;
        }
        
        // 计算目标时间�?        AVRational timeBase = formatCtx_->streams[videoStreamIndex_]->time_base;
        int64_t targetPts = av_rescale_q(positionMs * 1000, {1, 1000}, timeBase);
        
        // 定位到目标位�?        if (av_seek_frame(formatCtx_, videoStreamIndex_, targetPts, AVSEEK_FLAG_BACKWARD) < 0) {
            return false;
        }
        
        avcodec_flush_buffers(codecCtx_);
        return true;
    }
    
    QImage decodeNextFrame() {
        if (!formatCtx_ || !codecCtx_ || videoStreamIndex_ < 0) {
            return QImage();
        }
        
        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            return QImage();
        }
        
        // 读取并解码下一�?        while (av_read_frame(formatCtx_, packet) >= 0) {
            if (packet->stream_index == videoStreamIndex_) {
                if (avcodec_send_packet(codecCtx_, packet) == 0) {
                    int ret = avcodec_receive_frame(codecCtx_, frame_);
                    if (ret == 0) {
                        // 转换格式
                        sws_scale(swsCtx_, frame_->data, frame_->linesize, 0, height_,
                                 frameRGB_->data, frameRGB_->linesize);
                        
                        // 创建QImage
                        QImage image(frameRGB_->data[0], width_, height_, frameRGB_->linesize[0], QImage::Format_RGB888);
                        QImage result = image.copy();
                        
                        av_packet_free(&packet);
                        return result;
                    } else if (ret == AVERROR(EAGAIN)) {
                        // 需要更多数�?                        av_packet_unref(packet);
                        continue;
                    }
                }
            }
            av_packet_unref(packet);
        }
        
        av_packet_free(&packet);
        return QImage();
    }
    
    qint64 getCurrentPosition() {
        if (!formatCtx_ || !frame_ || videoStreamIndex_ < 0) {
            return 0;
        }
        
        AVRational timeBase = formatCtx_->streams[videoStreamIndex_]->time_base;
        int64_t pts = frame_->pts;
        if (pts == AV_NOPTS_VALUE) {
            return 0;
        }
        
        // 转换为毫�?        return av_rescale_q(pts, timeBase, {1, 1000});
    }
    
    qint64 duration() const { return duration_; }
    int width() const { return width_; }
    int height() const { return height_; }
    double fps() const { return fps_; }
    
private:
    void cleanup() {
        if (swsCtx_) {
            sws_freeContext(swsCtx_);
            swsCtx_ = nullptr;
        }
        if (buffer_) {
            av_free(buffer_);
            buffer_ = nullptr;
        }
        if (frameRGB_) {
            av_frame_free(&frameRGB_);
        }
        if (frame_) {
            av_frame_free(&frame_);
        }
        if (codecCtx_) {
            avcodec_free_context(&codecCtx_);
        }
        if (formatCtx_) {
            avformat_close_input(&formatCtx_);
        }
    }
    
    AVFormatContext* formatCtx_;
    AVCodecContext* codecCtx_;
    AVFrame* frame_;
    AVFrame* frameRGB_;
    SwsContext* swsCtx_;
    uint8_t* buffer_;
    int videoStreamIndex_;
    qint64 duration_;
    int width_;
    int height_;
    double fps_{30.0};
};

void MainWindow::openVideoPlayer(const QString& videoPath) {
    if (!QFile::exists(videoPath)) {
        QMessageBox::warning(this, tr("错误"), tr("视频文件不存在：%1").arg(videoPath));
        return;
    }
    
    // 检查FFplay是否可用（优先使用FFplay，因为Qt Multimedia后端可能不可用）
    QString appDir = QCoreApplication::applicationDirPath();
    QString ffplayPath = QDir(appDir).filePath("ffplay.exe");
    if (!QFile::exists(ffplayPath)) {
        // 尝试在PATH中查�?        ffplayPath = QStringLiteral("ffplay");
    }
    
    // 测试FFplay是否可用
    QProcess testProcess;
    testProcess.setProgram(ffplayPath);
    testProcess.setArguments({QStringLiteral("-version")});
    testProcess.start();
    bool ffplayAvailable = testProcess.waitForFinished(2000) && testProcess.exitCode() == 0;
    
    if (ffplayAvailable) {
        // 使用FFplay播放（更可靠，不依赖Qt Multimedia后端�?        openVideoPlayerWithFFplay(videoPath, ffplayPath);
        return;
    }
    
    // 如果FFplay不可用，尝试使用Qt Multimedia（可能失败）
    qWarning() << "[VideoPlayer] FFplay not available, falling back to Qt Multimedia (may not work)";
    
    // 创建视频播放器对话框
    QDialog* playerDialog = new QDialog(this);
    playerDialog->setWindowTitle(tr("视频播放�?- %1").arg(QFileInfo(videoPath).fileName()));
    playerDialog->setMinimumSize(1200, 800);
    playerDialog->setStyleSheet(QStringLiteral(
        "QDialog { background-color: #0f172a; }"
        "QLabel { color: #e2e8f0; }"
        "QPushButton { background-color: #3b82f6; color: white; border: none; padding: 8px 16px; min-width: 80px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #2563eb; }"
        "QPushButton:pressed { background-color: #1d4ed8; }"
        "QPushButton:disabled { background-color: #1e293b; color: #64748b; }"
        "QComboBox { background-color: #1e293b; color: #e2e8f0; border: 1px solid #3b82f6; padding: 4px 8px; border-radius: 4px; }"
        "QComboBox:hover { border-color: #2563eb; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { background-color: #1e293b; color: #e2e8f0; selection-background-color: #3b82f6; border: 1px solid #3b82f6; }"
        "QSlider::groove:horizontal { background: #1e293b; height: 6px; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #3b82f6; width: 16px; height: 16px; margin: -5px 0; border-radius: 8px; }"
        "QSlider::handle:horizontal:hover { background: #2563eb; }"
        "QSlider::sub-page:horizontal { background: #3b82f6; border-radius: 3px; }"));
    
    auto* mainLayout = new QVBoxLayout(playerDialog);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // 视频显示区域
    QVideoWidget* videoWidget = new QVideoWidget(playerDialog);
    videoWidget->setStyleSheet(QStringLiteral("background-color: #000000;"));
    videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
    videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(videoWidget, 1);
    
    // 控制面板
    auto* controlPanel = new QWidget(playerDialog);
    controlPanel->setStyleSheet(QStringLiteral("background-color: #1e293b; padding: 10px;"));
    auto* controlLayout = new QVBoxLayout(controlPanel);
    controlLayout->setContentsMargins(10, 10, 10, 10);
    controlLayout->setSpacing(8);
    
    // 进度条和时间显示
    auto* progressLayout = new QHBoxLayout();
    auto* timeLabel = new QLabel(tr("00:00 / 00:00"));
    timeLabel->setStyleSheet(QStringLiteral("color: #e2e8f0; min-width: 120px;"));
    progressLayout->addWidget(timeLabel);
    
    QSlider* positionSlider = new QSlider(Qt::Horizontal);
    positionSlider->setRange(0, 0);
    progressLayout->addWidget(positionSlider, 1);
    
    auto* speedLabel = new QLabel(tr("速度:"));
    speedLabel->setStyleSheet(QStringLiteral("color: #e2e8f0;"));
    progressLayout->addWidget(speedLabel);
    
    QComboBox* speedCombo = new QComboBox();
    speedCombo->addItems({tr("0.25x"), tr("0.5x"), tr("0.75x"), tr("1.0x"), tr("1.25x"), tr("1.5x"), tr("2.0x"), tr("4.0x")});
    speedCombo->setCurrentIndex(3);  // 默认1.0x
    speedCombo->setMaximumWidth(100);
    progressLayout->addWidget(speedCombo);
    
    controlLayout->addLayout(progressLayout);
    
    // 控制按钮
    auto* buttonLayout = new QHBoxLayout();
    
    QPushButton* playPauseButton = new QPushButton(tr("�?播放"));
    playPauseButton->setMinimumWidth(100);
    buttonLayout->addWidget(playPauseButton);
    
    QPushButton* stopButton = new QPushButton(tr("�?停止"));
    stopButton->setMinimumWidth(100);
    buttonLayout->addWidget(stopButton);
    
    buttonLayout->addSpacing(20);
    
    auto* volumeLabel = new QLabel(tr("音量:"));
    volumeLabel->setStyleSheet(QStringLiteral("color: #e2e8f0;"));
    buttonLayout->addWidget(volumeLabel);
    
    QSlider* volumeSlider = new QSlider(Qt::Horizontal);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(100);
    volumeSlider->setMaximumWidth(150);
    buttonLayout->addWidget(volumeSlider);
    
    auto* volumeValueLabel = new QLabel(tr("100%"));
    volumeValueLabel->setStyleSheet(QStringLiteral("color: #e2e8f0; min-width: 50px;"));
    buttonLayout->addWidget(volumeValueLabel);
    
    buttonLayout->addStretch();
    
    QPushButton* closeButton = new QPushButton(tr("关闭"));
    closeButton->setMinimumWidth(100);
    buttonLayout->addWidget(closeButton);
    
    controlLayout->addLayout(buttonLayout);
    
    mainLayout->addWidget(controlPanel);
    
    // 创建媒体播放�?    QMediaPlayer* mediaPlayer = new QMediaPlayer(playerDialog);
    QAudioOutput* audioOutput = new QAudioOutput(playerDialog);
    mediaPlayer->setAudioOutput(audioOutput);
    mediaPlayer->setVideoOutput(videoWidget);
    
    // 添加错误处理
    connect(mediaPlayer, &QMediaPlayer::errorOccurred, playerDialog, [playerDialog, mediaPlayer](QMediaPlayer::Error error, const QString& errorString) {
        qWarning() << "[VideoPlayer] Error:" << error << errorString;
        QMessageBox::warning(playerDialog, tr("播放错误"), 
            tr("无法播放视频�?1\n\n错误代码�?2\n\n请检查：\n1. 视频文件是否损坏\n2. 是否安装了必要的解码器\n3. 视频格式是否支持").arg(errorString).arg(error));
    });
    
    connect(mediaPlayer, &QMediaPlayer::mediaStatusChanged, playerDialog, [playerDialog, mediaPlayer](QMediaPlayer::MediaStatus status) {
        qInfo() << "[VideoPlayer] Media status changed:" << status;
        if (status == QMediaPlayer::InvalidMedia) {
            QMessageBox::warning(playerDialog, tr("无效媒体"), 
                tr("无法加载视频文件。\n\n可能的原因：\n1. 文件格式不支持\n2. 文件已损坏\n3. 缺少解码�?));
        } else if (status == QMediaPlayer::NoMedia) {
            qInfo() << "[VideoPlayer] No media loaded";
        } else if (status == QMediaPlayer::LoadedMedia) {
            qInfo() << "[VideoPlayer] Media loaded successfully, duration:" << mediaPlayer->duration() << "ms";
            qInfo() << "[VideoPlayer] Has video:" << mediaPlayer->hasVideo() << "Has audio:" << mediaPlayer->hasAudio();
        } else if (status == QMediaPlayer::BufferedMedia) {
            qInfo() << "[VideoPlayer] Media buffered";
        } else if (status == QMediaPlayer::BufferingMedia) {
            qInfo() << "[VideoPlayer] Media buffering...";
        }
    });
    
    // 设置视频�?    QUrl videoUrl = QUrl::fromLocalFile(videoPath);
    qInfo() << "[VideoPlayer] Setting source:" << videoUrl.toString();
    mediaPlayer->setSource(videoUrl);
    
    // 检查源是否有效
    if (mediaPlayer->source().isEmpty()) {
        QMessageBox::warning(playerDialog, tr("错误"), tr("无法设置视频源：%1").arg(videoPath));
        delete playerDialog;
        return;
    }
    
    // 更新播放/暂停按钮
    auto updatePlayButton = [playPauseButton](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::PlayingState) {
            playPauseButton->setText(tr("�?暂停"));
        } else {
            playPauseButton->setText(tr("�?播放"));
        }
    };
    
    // 连接信号
    connect(mediaPlayer, &QMediaPlayer::playbackStateChanged, playerDialog, updatePlayButton);
    connect(mediaPlayer, &QMediaPlayer::durationChanged, playerDialog, [positionSlider, timeLabel](qint64 duration) {
        positionSlider->setRange(0, static_cast<int>(duration));
        if (duration > 0) {
            int seconds = static_cast<int>(duration / 1000);
            int minutes = seconds / 60;
            seconds %= 60;
            int hours = minutes / 60;
            minutes %= 60;
            QString durationStr;
            if (hours > 0) {
                durationStr = QStringLiteral("%1:%2:%3").arg(hours, 2, 10, QChar('0'))
                                                         .arg(minutes, 2, 10, QChar('0'))
                                                         .arg(seconds, 2, 10, QChar('0'));
            } else {
                durationStr = QStringLiteral("%1:%2").arg(minutes, 2, 10, QChar('0'))
                                                      .arg(seconds, 2, 10, QChar('0'));
            }
            timeLabel->setText(QStringLiteral("00:00 / %1").arg(durationStr));
        }
    });
    
    connect(mediaPlayer, &QMediaPlayer::positionChanged, playerDialog, [positionSlider, timeLabel, mediaPlayer](qint64 position) {
        if (!positionSlider->isSliderDown()) {
            positionSlider->setValue(static_cast<int>(position));
        }
        
        // 更新时间显示
        qint64 duration = mediaPlayer->duration();
        if (duration > 0) {
            int posSeconds = static_cast<int>(position / 1000);
            int posMinutes = posSeconds / 60;
            posSeconds %= 60;
            int posHours = posMinutes / 60;
            posMinutes %= 60;
            
            int durSeconds = static_cast<int>(duration / 1000);
            int durMinutes = durSeconds / 60;
            durSeconds %= 60;
            int durHours = durMinutes / 60;
            durMinutes %= 60;
            
            QString posStr, durStr;
            if (posHours > 0 || durHours > 0) {
                posStr = QStringLiteral("%1:%2:%3").arg(posHours, 2, 10, QChar('0'))
                                                    .arg(posMinutes, 2, 10, QChar('0'))
                                                    .arg(posSeconds, 2, 10, QChar('0'));
                durStr = QStringLiteral("%1:%2:%3").arg(durHours, 2, 10, QChar('0'))
                                                    .arg(durMinutes, 2, 10, QChar('0'))
                                                    .arg(durSeconds, 2, 10, QChar('0'));
            } else {
                posStr = QStringLiteral("%1:%2").arg(posMinutes, 2, 10, QChar('0'))
                                                 .arg(posSeconds, 2, 10, QChar('0'));
                durStr = QStringLiteral("%1:%2").arg(durMinutes, 2, 10, QChar('0'))
                                                 .arg(durSeconds, 2, 10, QChar('0'));
            }
            timeLabel->setText(QStringLiteral("%1 / %2").arg(posStr, durStr));
        }
    });
    
    connect(positionSlider, &QSlider::sliderMoved, playerDialog, [mediaPlayer](int position) {
        mediaPlayer->setPosition(position);
    });
    
    connect(playPauseButton, &QPushButton::clicked, playerDialog, [mediaPlayer]() {
        if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
            mediaPlayer->pause();
        } else {
            mediaPlayer->play();
        }
    });
    
    connect(stopButton, &QPushButton::clicked, playerDialog, [mediaPlayer]() {
        mediaPlayer->stop();
    });
    
    connect(volumeSlider, &QSlider::valueChanged, playerDialog, [audioOutput, volumeValueLabel](int value) {
        audioOutput->setVolume(value / 100.0);
        volumeValueLabel->setText(QStringLiteral("%1%").arg(value));
    });
    
    connect(speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), playerDialog, [mediaPlayer, speedCombo](int index) {
        double speeds[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0};
        if (index >= 0 && index < 8) {
            mediaPlayer->setPlaybackRate(speeds[index]);
        }
    });
    
    connect(closeButton, &QPushButton::clicked, playerDialog, [playerDialog, mediaPlayer]() {
        mediaPlayer->stop();
        playerDialog->accept();
    });
    
    // 等待媒体加载完成后再播放
    if (mediaPlayer->mediaStatus() == QMediaPlayer::NoMedia) {
        // 如果还没有加载，等待加载完成
        QTimer::singleShot(500, playerDialog, [mediaPlayer]() {
            if (mediaPlayer->mediaStatus() == QMediaPlayer::LoadedMedia || 
                mediaPlayer->mediaStatus() == QMediaPlayer::BufferedMedia) {
                qInfo() << "[VideoPlayer] Auto-playing video";
                mediaPlayer->play();
            } else {
                qWarning() << "[VideoPlayer] Media not ready, status:" << mediaPlayer->mediaStatus();
            }
        });
    } else {
        // 如果已经加载，直接播�?        qInfo() << "[VideoPlayer] Media already loaded, playing immediately";
        mediaPlayer->play();
    }
    
    // 监听加载完成信号，自动播�?    connect(mediaPlayer, &QMediaPlayer::mediaStatusChanged, playerDialog, [mediaPlayer](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
            if (mediaPlayer->playbackState() == QMediaPlayer::StoppedState) {
                qInfo() << "[VideoPlayer] Media loaded, auto-playing";
                mediaPlayer->play();
            }
        }
    });
    
    playerDialog->exec();
    
    // 清理
    mediaPlayer->stop();
    delete playerDialog;
}

void MainWindow::openVideoPlayerWithFFplay(const QString& videoPath, const QString& ffplayPath) {
    // 创建完整的播放器界面
    QDialog* playerDialog = new QDialog(this);
    playerDialog->setWindowTitle(tr("视频播放�?- %1").arg(QFileInfo(videoPath).fileName()));
    playerDialog->setMinimumSize(1200, 800);
    playerDialog->setStyleSheet(QStringLiteral(
        "QDialog { background-color: #0f172a; }"
        "QLabel { color: #e2e8f0; }"
        "QPushButton { background-color: #3b82f6; color: white; border: none; padding: 8px 16px; min-width: 80px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #2563eb; }"
        "QPushButton:pressed { background-color: #1d4ed8; }"
        "QPushButton:disabled { background-color: #1e293b; color: #64748b; }"
        "QComboBox { background-color: #1e293b; color: #e2e8f0; border: 1px solid #3b82f6; padding: 4px 8px; border-radius: 4px; }"
        "QComboBox:hover { border-color: #2563eb; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { background-color: #1e293b; color: #e2e8f0; selection-background-color: #3b82f6; border: 1px solid #3b82f6; }"
        "QSlider::groove:horizontal { background: #1e293b; height: 6px; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #3b82f6; width: 16px; height: 16px; margin: -5px 0; border-radius: 8px; }"
        "QSlider::handle:horizontal:hover { background: #2563eb; }"
        "QSlider::sub-page:horizontal { background: #3b82f6; border-radius: 3px; }"));
    
    auto* mainLayout = new QVBoxLayout(playerDialog);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // 视频显示区域（使用FFmpeg解码直接显示，支持双击全屏）
    class VideoDisplayWidget : public QWidget {
    public:
        QImage currentFrame_;
        bool hasFrame_{false};
        
        explicit VideoDisplayWidget(QWidget* parent = nullptr) : QWidget(parent) {
            setStyleSheet(QStringLiteral("background-color: #000000;"));
        }

        void setFullscreenController(QDialog* dialog, bool* fullscreenFlag, QRect* normalGeometry) {
            dialog_ = dialog;
            isFullscreen_ = fullscreenFlag;
            normalGeometry_ = normalGeometry;
        }
        
    protected:
        void paintEvent(QPaintEvent*) override {
            QPainter painter(this);
            painter.fillRect(rect(), Qt::black);
            
            if (hasFrame_ && !currentFrame_.isNull()) {
                QSize widgetSize = size();
                QSize frameSize = currentFrame_.size();
                
                if (!frameSize.isEmpty()) {
                    // 检查是否全屏模式（通过检查窗口状态）
                    bool isFullscreen = false;
                    if (dialog_ && isFullscreen_) {
                        isFullscreen = *isFullscreen_;
                    }
                    
                    if (isFullscreen) {
                        // 全屏模式：填充整个屏幕，去除黑边（使�?KeepAspectRatioByExpanding 保持宽高比并填充整个区域�?                        QImage scaledImage = currentFrame_.scaled(widgetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                        // 计算居中裁剪区域
                        QRect sourceRect;
                        if (scaledImage.width() > widgetSize.width()) {
                            int x = (scaledImage.width() - widgetSize.width()) / 2;
                            sourceRect = QRect(x, 0, widgetSize.width(), scaledImage.height());
                        } else {
                            int y = (scaledImage.height() - widgetSize.height()) / 2;
                            sourceRect = QRect(0, y, scaledImage.width(), widgetSize.height());
                        }
                        painter.setRenderHint(QPainter::SmoothPixmapTransform);
                        painter.drawImage(rect(), scaledImage, sourceRect);
                    } else {
                        // 窗口模式：保持宽高比
                        QSize scaledSize = frameSize.scaled(widgetSize, Qt::KeepAspectRatio);
                        QRect targetRect((widgetSize.width() - scaledSize.width()) / 2,
                                        (widgetSize.height() - scaledSize.height()) / 2,
                                        scaledSize.width(), scaledSize.height());
                        painter.setRenderHint(QPainter::SmoothPixmapTransform);
                        painter.drawImage(targetRect, currentFrame_);
                    }
                }
            } else {
                // 显示加载提示
                painter.setPen(QColor(148, 163, 184));
                painter.setFont(QFont("Arial", 12));
                painter.drawText(rect(), Qt::AlignCenter, tr("正在加载视频..."));
            }
        }

        void mouseDoubleClickEvent(QMouseEvent* event) override {
            if (event->button() == Qt::LeftButton && dialog_ && isFullscreen_ && normalGeometry_) {
                if (!(*isFullscreen_)) {
                    // 保存当前窗口状态和位置
                    *normalGeometry_ = dialog_->geometry();
                    // 窗口最大化（使�?setWindowState 确保生效�?                    dialog_->setWindowState(Qt::WindowMaximized);
                    dialog_->show();
                    *isFullscreen_ = true;
                    // 更新视频显示
                    update();
                } else {
                    // 退出全屏，恢复窗口
                    dialog_->showNormal();
                    if (!normalGeometry_->isNull()) {
                        dialog_->setGeometry(*normalGeometry_);
                    }
                    *isFullscreen_ = false;
                    // 更新视频显示
                    update();
                }
            }
            QWidget::mouseDoubleClickEvent(event);
        }

    private:
        QDialog* dialog_{nullptr};
        bool* isFullscreen_{nullptr};
        QRect* normalGeometry_{nullptr};
    };

    bool isFullscreen = false;
    QRect normalGeometry;
    VideoDisplayWidget* videoWidget = new VideoDisplayWidget(playerDialog);
    videoWidget->setFullscreenController(playerDialog, &isFullscreen, &normalGeometry);
    videoWidget->setMinimumHeight(600);
    mainLayout->addWidget(videoWidget, 1);
    
    // FFmpeg解码�?    FFmpegVideoDecoder* decoder = new FFmpegVideoDecoder(playerDialog);
    if (!decoder->open(videoPath)) {
        QMessageBox::warning(playerDialog, tr("错误"), 
            tr("无法打开视频文件。\n\n可能的原因：\n1. 文件格式不支持\n2. 文件已损坏\n3. 缺少FFmpeg解码�?));
        delete playerDialog;
        return;
    }
    
    // 控制面板
    auto* controlPanel = new QWidget(playerDialog);
    controlPanel->setStyleSheet(QStringLiteral("background-color: #1e293b; padding: 10px;"));
    auto* controlLayout = new QVBoxLayout(controlPanel);
    controlLayout->setContentsMargins(10, 10, 10, 10);
    controlLayout->setSpacing(8);
    
    // 进度条和时间显示
    auto* progressLayout = new QHBoxLayout();
    auto* timeLabel = new QLabel(tr("00:00 / 00:00"));
    timeLabel->setStyleSheet(QStringLiteral("color: #e2e8f0; min-width: 120px; font-size: 10pt;"));
    progressLayout->addWidget(timeLabel);
    
    QSlider* positionSlider = new QSlider(Qt::Horizontal);
    positionSlider->setRange(0, static_cast<int>(decoder->duration()));
    progressLayout->addWidget(positionSlider, 1);
    
    // 更新总时长显�?    if (decoder->duration() > 0) {
        int seconds = static_cast<int>(decoder->duration() / 1000);
        int minutes = seconds / 60;
        seconds %= 60;
        int hours = minutes / 60;
        minutes %= 60;
        QString durationStr;
        if (hours > 0) {
            durationStr = QStringLiteral("%1:%2:%3").arg(hours, 2, 10, QChar('0'))
                                                     .arg(minutes, 2, 10, QChar('0'))
                                                     .arg(seconds, 2, 10, QChar('0'));
        } else {
            durationStr = QStringLiteral("%1:%2").arg(minutes, 2, 10, QChar('0'))
                                                  .arg(seconds, 2, 10, QChar('0'));
        }
        timeLabel->setText(QStringLiteral("00:00 / %1").arg(durationStr));
    }
    
    controlLayout->addLayout(progressLayout);
    
    // 工具�?    auto* toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(10);
    
    // 播放控制按钮
    QPushButton* playPauseButton = new QPushButton(tr("�?播放"));
    playPauseButton->setMinimumWidth(100);
    toolbarLayout->addWidget(playPauseButton);
    
    QPushButton* stopButton = new QPushButton(tr("�?停止"));
    stopButton->setMinimumWidth(100);
    toolbarLayout->addWidget(stopButton);
    
    toolbarLayout->addSpacing(20);
    
    // 音量控制（FFplay不支持外部音量控制，显示提示�?    auto* volumeLabel = new QLabel(tr("音量:"));
    volumeLabel->setStyleSheet(QStringLiteral("color: #e2e8f0;"));
    toolbarLayout->addWidget(volumeLabel);
    
    QSlider* volumeSlider = new QSlider(Qt::Horizontal);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(100);
    volumeSlider->setMaximumWidth(150);
    volumeSlider->setEnabled(false);  // FFplay不支持外部音量控�?    toolbarLayout->addWidget(volumeSlider);
    
    auto* volumeValueLabel = new QLabel(tr("100%"));
    volumeValueLabel->setStyleSheet(QStringLiteral("color: #e2e8f0; min-width: 50px;"));
    toolbarLayout->addWidget(volumeValueLabel);
    
    toolbarLayout->addSpacing(20);
    
    // 速度选择
    auto* speedLabel = new QLabel(tr("播放速度:"));
    speedLabel->setStyleSheet(QStringLiteral("color: #e2e8f0; font-weight: bold;"));
    toolbarLayout->addWidget(speedLabel);
    
    QComboBox* speedCombo = new QComboBox();
    speedCombo->addItems({tr("0.25x"), tr("0.5x"), tr("0.75x"), tr("1.0x"), tr("1.25x"), tr("1.5x"), tr("2.0x"), tr("4.0x")});
    speedCombo->setCurrentIndex(3);  // 默认1.0x
    speedCombo->setMinimumWidth(120);
    toolbarLayout->addWidget(speedCombo);
    
    // 当前速度显示
    auto* currentSpeedLabel = new QLabel(tr("当前: 1.0x"));
    currentSpeedLabel->setStyleSheet(QStringLiteral("color: #3b82f6; font-weight: bold; min-width: 70px;"));
    toolbarLayout->addWidget(currentSpeedLabel);

    toolbarLayout->addSpacing(20);

    // 全屏按钮
    QPushButton* fullscreenButton = new QPushButton(tr("全屏"));
    fullscreenButton->setMinimumWidth(100);
    toolbarLayout->addWidget(fullscreenButton);

    toolbarLayout->addStretch();
    
    QPushButton* closeButton = new QPushButton(tr("关闭"));
    closeButton->setMinimumWidth(100);
    toolbarLayout->addWidget(closeButton);
    
    controlLayout->addLayout(toolbarLayout);
    
    mainLayout->addWidget(controlPanel);
    
    // 播放控制
    qint64 currentPosition = 0;
    bool isPlaying = false;
    double currentSpeed = 1.0;
    QTimer* playTimer = new QTimer(playerDialog);
    
    // 更新视频帧的函数（连续解码）
    auto updateFrame = [&]() {
        QImage frame = decoder->decodeNextFrame();
        if (!frame.isNull()) {
            videoWidget->currentFrame_ = frame;
            videoWidget->hasFrame_ = true;
            videoWidget->update();  // 触发重绘
            
            // 更新当前位置
            currentPosition = decoder->getCurrentPosition();
        } else {
            // 解码失败或到达末�?            if (currentPosition >= decoder->duration()) {
                isPlaying = false;
                playPauseButton->setText(tr("�?播放"));
                playTimer->stop();
            }
        }
    };
    
    // 播放定时器（使用实际帧率�?    connect(playTimer, &QTimer::timeout, playerDialog, [&]() {
        if (isPlaying) {
            // 解码下一�?            updateFrame();
            
            // 检查是否到达末�?            if (currentPosition >= decoder->duration()) {
                isPlaying = false;
                playPauseButton->setText(tr("�?播放"));
                playTimer->stop();
                currentPosition = decoder->duration();
            }
            
            // 更新进度条和时间
            if (!positionSlider->isSliderDown()) {
                positionSlider->setValue(static_cast<int>(currentPosition));
            }
            
            // 更新时间显示
            int posSeconds = static_cast<int>(currentPosition / 1000);
            int posMinutes = posSeconds / 60;
            posSeconds %= 60;
            int posHours = posMinutes / 60;
            posMinutes %= 60;
            
            int durSeconds = static_cast<int>(decoder->duration() / 1000);
            int durMinutes = durSeconds / 60;
            durSeconds %= 60;
            int durHours = durMinutes / 60;
            durMinutes %= 60;
            
            QString posStr, durStr;
            if (posHours > 0 || durHours > 0) {
                posStr = QStringLiteral("%1:%2:%3").arg(posHours, 2, 10, QChar('0'))
                                                    .arg(posMinutes, 2, 10, QChar('0'))
                                                    .arg(posSeconds, 2, 10, QChar('0'));
                durStr = QStringLiteral("%1:%2:%3").arg(durHours, 2, 10, QChar('0'))
                                                    .arg(durMinutes, 2, 10, QChar('0'))
                                                    .arg(durSeconds, 2, 10, QChar('0'));
            } else {
                posStr = QStringLiteral("%1:%2").arg(posMinutes, 2, 10, QChar('0'))
                                                 .arg(posSeconds, 2, 10, QChar('0'));
                durStr = QStringLiteral("%1:%2").arg(durMinutes, 2, 10, QChar('0'))
                                                 .arg(durSeconds, 2, 10, QChar('0'));
            }
            timeLabel->setText(QStringLiteral("%1 / %2").arg(posStr, durStr));
        }
    });
    
    // 播放/暂停按钮
    connect(playPauseButton, &QPushButton::clicked, playerDialog, [&]() {
        if (isPlaying) {
            isPlaying = false;
            playPauseButton->setText(tr("�?播放"));
            playTimer->stop();
        } else {
            // 如果到达末尾，重新开�?            if (currentPosition >= decoder->duration()) {
                currentPosition = 0;
                decoder->seekTo(0);
            }
            isPlaying = true;
            playPauseButton->setText(tr("�?暂停"));
            // 根据视频实际帧率和速度调整定时器间�?            double actualFps = decoder->fps();
            int interval = static_cast<int>(1000.0 / actualFps / currentSpeed);
            if (interval < 1) interval = 1;  // 最�?ms
            playTimer->start(interval);
            updateFrame();  // 立即更新一�?        }
    });
    
    // 停止按钮
    connect(stopButton, &QPushButton::clicked, playerDialog, [&]() {
        isPlaying = false;
        playPauseButton->setText(tr("�?播放"));
        playTimer->stop();
        currentPosition = 0;
        decoder->seekTo(0);
        positionSlider->setValue(0);
        timeLabel->setText(QStringLiteral("00:00 / %1").arg(timeLabel->text().split(" / ").last()));
        updateFrame();
    });
    
    // 进度条拖�?
    connect(positionSlider, &QSlider::sliderMoved, playerDialog, [&](int position) {
        currentPosition = position;
        decoder->seekTo(currentPosition);
        updateFrame();
    });
    
    // 速度控制
    connect(speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), playerDialog, [&](int index) {
        double speeds[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0};
        if (index >= 0 && index < 8) {
            currentSpeed = speeds[index];
            currentSpeedLabel->setText(tr("当前: %1x").arg(currentSpeed, 0, 'f', 2));
            // 如果正在播放，更新定时器间隔（使用实际帧率）
            if (isPlaying) {
                double actualFps = decoder->fps();
                int interval = static_cast<int>(1000.0 / actualFps / currentSpeed);
                if (interval < 1) interval = 1;  // 最�?ms
                playTimer->setInterval(interval);
            }
        }
    });
    
    // 全屏按钮点击：窗口最大化（全屏效果）
    connect(fullscreenButton, &QPushButton::clicked, playerDialog, [playerDialog, &isFullscreen, &normalGeometry, fullscreenButton, videoWidget]() {
        if (!isFullscreen) {
            // 保存当前窗口状态和位置
            normalGeometry = playerDialog->geometry();
            // 窗口最大化（使�?setWindowState 确保生效�?            playerDialog->setWindowState(Qt::WindowMaximized);
            playerDialog->show();
            fullscreenButton->setText(QObject::tr("退出全�?));
            isFullscreen = true;
            // 更新视频显示
            if (videoWidget) {
                videoWidget->update();
            }
        } else {
            // 退出全屏，恢复窗口
            playerDialog->setWindowState(Qt::WindowNoState);
            playerDialog->showNormal();
            if (!normalGeometry.isNull()) {
                playerDialog->setGeometry(normalGeometry);
            }
            fullscreenButton->setText(QObject::tr("全屏"));
            isFullscreen = false;
            // 更新视频显示
            if (videoWidget) {
                videoWidget->update();
            }
        }
    });
    
    // ESC键退出全�?    auto* escapeAction = new QAction(playerDialog);
    escapeAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(escapeAction, &QAction::triggered, playerDialog, [playerDialog, &isFullscreen, &normalGeometry, fullscreenButton, videoWidget]() {
        if (isFullscreen) {
            playerDialog->showNormal();
            if (!normalGeometry.isNull()) {
                playerDialog->setGeometry(normalGeometry);
            }
            fullscreenButton->setText(QObject::tr("全屏"));
            isFullscreen = false;
            // 更新视频显示
            if (videoWidget) {
                videoWidget->update();
            }
        }
    });
    playerDialog->addAction(escapeAction);
    
    // 关闭按钮
    connect(closeButton, &QPushButton::clicked, playerDialog, [playerDialog, playTimer]() {
        playTimer->stop();
        playerDialog->accept();
    });
    
    // 定位到开头并加载第一�?    decoder->seekTo(0);
    updateFrame();
    
    playerDialog->exec();
    
    // 清理
    playTimer->stop();
    delete playerDialog;
}

// 纯UDP模式：WebSocket重连功能已废�?
// void MainWindow::performWebSocketReconnect(const DiscoveredClient& client) {
//     // 此功能已被纯UDP架构替代，不再需要WebSocket连接
// }

void MainWindow::appendLayoutActions(QMenu* menu) {
    if (!menu) {
        return;
    }

    auto groupLabel = [](int groupIndex, int maxRows) {
        const int startRow = groupIndex * 4 + 1;
        const int endRow = qMin(maxRows, startRow + 3);
        return QStringLiteral("%1-%2 行").arg(startRow).arg(endRow);
    };

    int lastGroup = -1;
    const int maxRows = 20;
    for (QAction* act : layoutPresetActions_) {
        if (!act) {
            continue;
        }
        const int rows = act->property("layoutRows").toInt();
        const int groupIndex = rows > 0 ? (rows - 1) / 4 : 0;
        if (groupIndex != lastGroup) {
            menu->addSection(groupLabel(groupIndex, maxRows));
            lastGroup = groupIndex;
        }
        menu->addAction(act);
    }
}

void MainWindow::updateStatusBarStats() {
    if (!metricsLabel_ || !errorLabel_) {
        return;
    }

    const int streamCount = tileStats_.size();
    double fpsSum = 0.0;
    double mbpsSum = 0.0;
    for (auto it = tileStats_.cbegin(); it != tileStats_.cend(); ++it) {
        fpsSum += it.value().fps;
        mbpsSum += it.value().mbps;
    }
    const double fpsAvg = streamCount > 0 ? fpsSum / static_cast<double>(streamCount) : 0.0;

    const QString fpsText = streamCount > 0 ? QString::number(fpsAvg, 'f', 1) : QStringLiteral("--");
    const QString mbpsText = streamCount > 0 ? QString::number(mbpsSum, 'f', 2) : QStringLiteral("--");

    metricsLabel_->setText(tr("监控: %1 | 平均帧率: %2 fps | 总码�? %3 Mbps")
                               .arg(streamCount)
                               .arg(fpsText)
                               .arg(mbpsText));

    QString latestError = lastErrorMessage_;
    if (latestError.isEmpty() && !lastErrorTexts_.isEmpty()) {
        const auto it = lastErrorTexts_.constBegin();
        latestError = QStringLiteral("%1: %2").arg(it.key(), it.value());
    }
    if (latestError.isEmpty()) {
        errorLabel_->setText(tr("最近异常: 无"));
    } else {
        errorLabel_->setText(tr("最近异常: %1").arg(latestError));
    }
    updateWallHeaderStats();
}

QString MainWindow::DefaultGroup() {
    return QStringLiteral("未分组");
}

void MainWindow::loadClientMetadata() {
    groupNames_.clear();
    groupNames_.insert(DefaultGroup());
    clientGroupsCache_.clear();
    clientRemarksCache_.clear();

    QFile file(metadataPath_);
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[Console] Failed to open metadata" << metadataPath_ << file.errorString();
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        qWarning() << "[Console] Metadata is not an object";
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonArray groupsArray = root.value(QStringLiteral("groups")).toArray();
    for (const QJsonValue& value : groupsArray) {
        if (value.isString()) {
            const QString groupName = value.toString().trimmed();
            if (!groupName.isEmpty()) {
                groupNames_.insert(groupName);
            }
        }
    }
    if (!groupNames_.contains(DefaultGroup())) {
        groupNames_.insert(DefaultGroup());
    }

    const QJsonObject clientsObj = root.value(QStringLiteral("clients")).toObject();
    for (auto it = clientsObj.constBegin(); it != clientsObj.constEnd(); ++it) {
        const QString clientId = it.key();
        const QJsonObject obj = it.value().toObject();
        const QString group = obj.value(QStringLiteral("group")).toString().trimmed();
        const QString remark = obj.value(QStringLiteral("remark")).toString().trimmed();
        if (!group.isEmpty()) {
            groupNames_.insert(group);
            clientGroupsCache_.insert(clientId, group);
        }
        if (!remark.isEmpty()) {
            clientRemarksCache_.insert(clientId, remark);
        }
    }

    populateGroupFilterOptions();
}

void MainWindow::saveClientMetadata() const {
    QJsonObject root;
    QJsonArray groupsArray;
    QStringList groups = groupNames_.values();
    groups.removeAll(DefaultGroup());
    std::sort(groups.begin(), groups.end(), [](const QString& a, const QString& b) {
        return a.localeAwareCompare(b) < 0;
    });
    groups.prepend(DefaultGroup());
    for (const QString& group : groups) {
        groupsArray.append(group);
    }
    root.insert(QStringLiteral("groups"), groupsArray);

    QJsonObject clientsObj;
    QSet<QString> allClientIds;
    for (const QString& key : clientRemarksCache_.keys()) {
        allClientIds.insert(key);
    }
    for (const QString& key : clientGroupsCache_.keys()) {
        allClientIds.insert(key);
    }
    for (auto it = clientEntries_.cbegin(); it != clientEntries_.cend(); ++it) {
        allClientIds.insert(it.key());
    }
    for (const QString& clientId : allClientIds) {
        QJsonObject obj;
        const QString group = clientGroupsCache_.value(clientId, clientEntries_.value(clientId).group);
        const QString remark = clientRemarksCache_.value(clientId, clientEntries_.value(clientId).remark);
        const QString normalizedGroup = group.isEmpty() ? DefaultGroup() : group;
        if (!normalizedGroup.isEmpty() && normalizedGroup != DefaultGroup()) {
            obj.insert(QStringLiteral("group"), normalizedGroup);
        }
        if (!remark.isEmpty()) {
            obj.insert(QStringLiteral("remark"), remark);
        }
        if (!obj.isEmpty()) {
            clientsObj.insert(clientId, obj);
        }
    }
    root.insert(QStringLiteral("clients"), clientsObj);

    QFileInfo info(metadataPath_);
    if (!info.dir().exists()) {
        QDir().mkpath(info.dir().absolutePath());
    }

    QFile file(metadataPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "[Console] Failed to write metadata" << metadataPath_ << file.errorString();
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

QTreeWidgetItem* MainWindow::ensureGroupItem(const QString& groupName) {
    const QString normalized = groupName.isEmpty() ? DefaultGroup() : groupName;
    if (groupItems_.contains(normalized)) {
        return groupItems_.value(normalized);
    }
    auto* item = new QTreeWidgetItem(clientTree_);
    item->setData(0, kRoleType, kItemTypeGroup);
    item->setData(0, kRoleGroupName, normalized);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled);
    item->setText(0, normalized);
    groupItems_.insert(normalized, item);
    return item;
}

QString MainWindow::groupForClient(const QString& clientId) const {
    const auto it = clientEntries_.find(clientId);
    if (it != clientEntries_.end() && !it->group.isEmpty()) {
        return it->group;
    }
    return clientGroupsCache_.value(clientId, DefaultGroup());
}

QString MainWindow::remarkForClient(const QString& clientId) const {
    const auto it = clientEntries_.find(clientId);
    if (it != clientEntries_.end() && !it->remark.isEmpty()) {
        return it->remark;
    }
    return clientRemarksCache_.value(clientId);
}

void MainWindow::scheduleClientTreeRebuild() {
    // 标记需要重建，由定时器批量处理
    clientTreeNeedsRebuild_ = true;
    if (!clientTreeRebuildTimer_->isActive()) {
        clientTreeRebuildTimer_->start();
    }
}

void MainWindow::rebuildClientTree() {
    // 性能优化：禁用更新，批量重建
    clientTree_->setUpdatesEnabled(false);
    
    // 优化：只在客户端数量变化或分组变化时才完全重�?    const int currentClientCount = clientEntries_.size();
    const int currentGroupCount = groupNames_.size();
    
    // 预分配容量，减少内存重新分配
    groupItems_.clear();
    clientItems_.clear();
    clientTree_->clear();

    // 批量更新客户端条目数据（避免在循环中多次查找�?    QMap<QString, QString> normalizedGroups;
    for (auto it = clientEntries_.begin(); it != clientEntries_.end(); ++it) {
        it->group = clientGroupsCache_.value(it.key(), it->group.isEmpty() ? DefaultGroup() : it->group);
        it->remark = clientRemarksCache_.value(it.key(), it->remark);
        const QString normalizedGroup = it->group.isEmpty() ? DefaultGroup() : it->group;
        normalizedGroups.insert(it.key(), normalizedGroup);
        groupNames_.insert(normalizedGroup);
    }

    // 优化：预排序分组列表
    QStringList groups = groupNames_.values();
    groups.removeAll(DefaultGroup());
    std::sort(groups.begin(), groups.end(), [](const QString& a, const QString& b) {
        return a.localeAwareCompare(b) < 0;
    });
    groups.prepend(DefaultGroup());

    // 批量创建分组�?    for (const QString& group : groups) {
        ensureGroupItem(group);
    }

    // 优化：批量创建客户端项，减少布局计算
    QVector<QTreeWidgetItem*> newClientItems;
    newClientItems.reserve(currentClientCount);
    
    for (auto it = clientEntries_.cbegin(); it != clientEntries_.cend(); ++it) {
        const QString clientId = it.key();
        const ClientEntry& entry = it.value();
        const QString& normalizedGroup = normalizedGroups.value(clientId, DefaultGroup());
        QTreeWidgetItem* groupItem = ensureGroupItem(normalizedGroup);
        
        auto* clientItem = new QTreeWidgetItem(groupItem);
        clientItem->setData(0, kRoleType, kItemTypeClient);
        clientItem->setData(0, kRoleClientId, clientId);
        clientItem->setData(0, kRoleGroupName, groupItem->data(0, kRoleGroupName));
        clientItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        
        // 优化：避免重复字符串拼接
        QString displayName = entry.remark.isEmpty() ? clientId : QStringLiteral("%1 (%2)").arg(clientId, entry.remark);
        clientItem->setText(0, displayName);
        clientItem->setForeground(0, entry.online ? QBrush(QColor(0, 220, 0)) : QBrush(QColor(220, 0, 0)));
        
        clientItems_.insert(clientId, clientItem);
        newClientItems.append(clientItem);
    }

    // 批量展开分组
    for (auto* item : groupItems_) {
        if (item) {
            item->setExpanded(true);
        }
    }

    // 优化：批量更�?tile 显示名称（减少查找次数）
    for (auto it = activeTiles_.cbegin(); it != activeTiles_.cend(); ++it) {
        updateTileDisplayName(it.key());
    }

    clientTree_->expandAll();
    clientTree_->setUpdatesEnabled(true);
    
    // 更新分组过滤器选项
    populateGroupFilterOptions();
}

void MainWindow::handleClientDropped(const QString& clientId, const QString& newGroup) {
    const QString normalized = newGroup.isEmpty() ? DefaultGroup() : newGroup;
    auto it = clientEntries_.find(clientId);
    if (it == clientEntries_.end()) {
        ClientEntry entry;
        entry.group = normalized;
        entry.online = false;
        clientEntries_.insert(clientId, entry);
        it = clientEntries_.find(clientId);
    }
    if (it->group == normalized) {
        return;
    }
    it->group = normalized;
    if (normalized == DefaultGroup()) {
        clientGroupsCache_.remove(clientId);
    } else {
        clientGroupsCache_.insert(clientId, normalized);
    }
    groupNames_.insert(normalized);
    scheduleClientTreeRebuild();  // 批量更新
    saveClientMetadata();
}

void MainWindow::addGroup() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("新增分组"), tr("分组名称"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    if (name == DefaultGroup()) {
        QMessageBox::warning(this, tr("新增分组"), tr("该名称已保留，请换一个名�?));
        return;
    }
    if (groupNames_.contains(name)) {
        QMessageBox::warning(this, tr("新增分组"), tr("分组 %1 已存�?).arg(name));
        return;
    }
    groupNames_.insert(name);
    scheduleClientTreeRebuild();  // 批量更新
    saveClientMetadata();
}

void MainWindow::renameGroup(QTreeWidgetItem* groupItem) {
    if (!groupItem) {
        return;
    }
    const QString oldName = groupItem->data(0, kRoleGroupName).toString();
    if (oldName == DefaultGroup()) {
        QMessageBox::information(this, tr("重命名分�?), tr("默认分组无法重命名�?));
        return;
    }
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("重命名分�?), tr("新的分组名称"), QLineEdit::Normal, oldName, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == oldName) {
        return;
    }
    if (newName == DefaultGroup()) {
        QMessageBox::warning(this, tr("重命名分�?), tr("该名称已保留，请换一个名称�?));
        return;
    }
    if (groupNames_.contains(newName)) {
        QMessageBox::warning(this, tr("重命名分�?), tr("分组 %1 已存在�? ).arg(newName));
        return;
    }

    groupNames_.remove(oldName);
    groupNames_.insert(newName);

    for (auto it = clientEntries_.begin(); it != clientEntries_.end(); ++it) {
        if (it->group == oldName) {
            it->group = newName;
        }
    }
    for (auto it = clientGroupsCache_.begin(); it != clientGroupsCache_.end(); ++it) {
        if (it.value() == oldName) {
            it.value() = newName;
        }
    }

    rebuildClientTree();
    saveClientMetadata();
}

void MainWindow::removeGroup(QTreeWidgetItem* groupItem) {
    if (!groupItem) {
        return;
    }
    const QString groupName = groupItem->data(0, kRoleGroupName).toString();
    if (groupName == DefaultGroup()) {
        QMessageBox::information(this, tr("删除分组"), tr("默认分组无法删除"));
        return;
    }
    for (auto it = clientEntries_.cbegin(); it != clientEntries_.cend(); ++it) {
        if (it->group == groupName) {
            QMessageBox::information(this, tr("删除分组"), tr("分组 %1 仍包含客户端，无法删�?).arg(groupName));
            return;
        }
    }
    if (clientGroupsCache_.values().contains(groupName)) {
        QMessageBox::information(this, tr("删除分组"), tr("分组 %1 仍在使用，无法删�?).arg(groupName));
        return;
    }
    groupNames_.remove(groupName);
    scheduleClientTreeRebuild();  // 批量更新
    saveClientMetadata();
}

void MainWindow::editClientRemark(const QString& clientId) {
    auto it = clientEntries_.find(clientId);
    if (it == clientEntries_.end()) {
        return;
    }
    bool ok = false;
    const QString current = it->remark;
    QString remark = QInputDialog::getText(this, tr("编辑备注"), tr("请输入备�?), QLineEdit::Normal, current, &ok);
    if (!ok) {
        return;
    }
    remark = remark.trimmed();
    
    // Save to REST API
    saveClientRemarkToRestApi(clientId, remark);
    
    // Update local cache immediately for UI responsiveness
    it->remark = remark;
    if (remark.isEmpty()) {
        clientRemarksCache_.remove(clientId);
    } else {
        clientRemarksCache_.insert(clientId, remark);
    }
    updateClientTreeItem(clientId);
    scheduleClientTreeRebuild();  // 批量更新
    saveClientMetadata();
    updateTileDisplayName(clientId);
}

void MainWindow::openClientDetails(const QString& clientId) {
    if (clientId.isEmpty()) {
        return;
    }

    ClientEntry entry = clientEntries_.value(clientId);
    QString displayName;
    if (QTreeWidgetItem* item = clientItems_.value(clientId, nullptr)) {
        displayName = item->text(0);
    }
    if (displayName.isEmpty()) {
        const QString remark = entry.remark;
        displayName = remark.isEmpty() ? clientId : QStringLiteral("%1 (%2)").arg(clientId, remark);
    }

    if (activeDetailsDialog_ && !activeDetailsDialog_.isNull()) {
        activeDetailsDialog_->close();
    }

    auto* dialog =
        new ClientDetailsDialog(clientId, displayName, db_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    activeDetailsDialog_ = dialog;
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::updateTileDisplayName(const QString& clientId) {
    StreamTile* tile = activeTiles_.value(clientId, nullptr);
    if (!tile) {
        return;
    }
    QString displayName;
    if (QTreeWidgetItem* item = clientItems_.value(clientId, nullptr)) {
        displayName = item->text(0);
    }
    if (displayName.isEmpty()) {
        const QString remark = remarkForClient(clientId);
        displayName = remark.isEmpty() ? clientId : QStringLiteral("%1 (%2)").arg(clientId, remark);
    }
    const StreamStats stats = tileStats_.value(clientId);
    tile->setStats(stats.fps, stats.mbps);
    tile->setErrorMessage(lastErrorTexts_.value(clientId));
    tile->setDisplayName(displayName);
}

void MainWindow::updateWallHeaderStats() {
    if (!wallStatsLabel_) {
        return;
    }
    const int totalClients = clientEntries_.size();
    int onlineClients = 0;
    for (auto it = clientEntries_.cbegin(); it != clientEntries_.cend(); ++it) {
        if (it->online) {
            ++onlineClients;
        }
    }
    const int previewCount = activePlayers_.size();
    const int tileCount = activeTiles_.size();
    const QString filterText =
        currentGroupFilter_ == QStringLiteral("ALL") ? tr("全部") : currentGroupFilter_;
    wallStatsLabel_->setText(tr("监控�?(%1) | 在线 %2 / %3 | 预览 %4 | 活跃 %5")
                                 .arg(filterText)
                                 .arg(onlineClients)
                                 .arg(totalClients)
                                 .arg(previewCount)
                                 .arg(tileCount));
}

void MainWindow::handleGroupFilterChanged(int index) {
    if (!groupFilterCombo_) {
        return;
    }
    const QString filter = groupFilterCombo_->itemData(index).toString();
    currentGroupFilter_ = filter.isEmpty() ? QStringLiteral("ALL") : filter;
    syncPreviewWithFilter();
}

void MainWindow::populateGroupFilterOptions() {
    if (!groupFilterCombo_) {
        return;
    }
    QString previous = currentGroupFilter_;
    QStringList groups = groupNames_.values();
    groups.removeAll(DefaultGroup());
    std::sort(groups.begin(), groups.end(), [](const QString& a, const QString& b) {
        return a.localeAwareCompare(b) < 0;
    });

    QSignalBlocker blocker(groupFilterCombo_);
    groupFilterCombo_->clear();
    groupFilterCombo_->addItem(tr("全部客户�?), QStringLiteral("ALL"));
    groupFilterCombo_->addItem(DefaultGroup(), DefaultGroup());
    for (const QString& group : groups) {
        groupFilterCombo_->addItem(group, group);
    }

    int index = groupFilterCombo_->findData(previous);
    if (index < 0) {
        index = 0;
    }
    groupFilterCombo_->setCurrentIndex(index);
    currentGroupFilter_ = groupFilterCombo_->itemData(index).toString();
}

QString MainWindow::computeDisplayName(const QString& clientId) const {
    const auto it = clientEntries_.find(clientId);
    if (it != clientEntries_.end() && !it->remark.isEmpty()) {
        return QStringLiteral("%1 (%2)").arg(clientId, it->remark);
    }
    if (QTreeWidgetItem* item = clientItems_.value(clientId, nullptr)) {
        return item->text(0);
    }
    return clientId;
}

QStringList MainWindow::orderedClientIdsForFilter(const QString& filter) const {
    QStringList result;
    auto appendGroup = [&](const QString& groupName) {
        QStringList ids;
        for (auto it = clientEntries_.cbegin(); it != clientEntries_.cend(); ++it) {
            const QString normalizedGroup =
                it->group.isEmpty() ? DefaultGroup() : it->group;
            if (!it->online || it->ssrc == 0 || normalizedGroup != groupName) {
                continue;
            }
            ids.append(it.key());
        }
        std::sort(ids.begin(), ids.end(), [this](const QString& a, const QString& b) {
            return computeDisplayName(a).localeAwareCompare(computeDisplayName(b)) < 0;
        });
        result.append(ids);
    };

    if (filter == QStringLiteral("ALL")) {
        QStringList groups = groupNames_.values();
        groups.removeAll(DefaultGroup());
        std::sort(groups.begin(), groups.end(), [](const QString& a, const QString& b) {
            return a.localeAwareCompare(b) < 0;
        });
        appendGroup(DefaultGroup());
        for (const QString& group : groups) {
            appendGroup(group);
        }
    } else {
        appendGroup(filter);
    }
    return result;
}

void MainWindow::syncPreviewWithFilter() {
    const QStringList desired = orderedClientIdsForFilter(currentGroupFilter_);
    QSet<QString> desiredSet;
    for (const QString& id : desired) {
        desiredSet.insert(id);
    }

    const QStringList activeKeys = activePlayers_.keys();
    for (const QString& id : activeKeys) {
        if (!desiredSet.contains(id)) {
            stopPreview(id);
        }
    }

    for (const QString& id : desired) {
        if (!activePlayers_.contains(id)) {
            const ClientEntry entry = clientEntries_.value(id);
            if (entry.online && entry.ssrc != 0) {
                startPreview(id, entry.ssrc);
            }
        }
    }

    layoutOrder_.clear();
    for (const QString& id : desired) {
        if (activeTiles_.contains(id)) {
            layoutOrder_.append(id);
        }
    }
    rebuildPreviewLayout();
    // updateStatusBarStats() 会在定时器中自动更新，不需要立即调�?}

void MainWindow::schedulePreviewRelayout() {
    if (layoutRefreshPending_) {
        return;
    }
    layoutRefreshPending_ = true;
    // 使用 50ms 延迟，避免频繁重建布局
    QTimer::singleShot(50, this, [this]() {
        layoutRefreshPending_ = false;
        rebuildPreviewLayout();
    });
}

void MainWindow::saveClientRemarkToRestApi(const QString& clientId, const QString& remark) {
    if (!restApiManager_) {
        qWarning() << "[Console] REST API manager not initialized";
        return;
    }
    
    QString restUrl = config_.restApiUrl();
    if (restUrl.isEmpty()) {
        qWarning() << "[Console] REST API URL not configured";
        return;
    }
    
    QUrl url(restUrl);
    if (!url.isValid()) {
        qWarning() << "[Console] Invalid REST API URL:" << restUrl;
        return;
    }
    
    QString path = QStringLiteral("/api/client/%1/remark").arg(QString::fromUtf8(QUrl::toPercentEncoding(clientId)));
    url.setPath(path);
    
    QJsonObject payload;
    payload.insert(QStringLiteral("remark"), remark);
    
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(10000);  // 10 seconds timeout
    
    QNetworkReply* reply = restApiManager_->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    reply->setProperty("clientId", clientId);
    reply->setProperty("remark", remark);
    reply->setProperty("requestType", QStringLiteral("saveRemark"));
    
    qInfo() << "[Console] Saving remark for" << clientId << "via REST API:" << url.toString();
}

void MainWindow::handleRestApiReply(QNetworkReply* reply) {
    if (!reply) {
        return;
    }
    
    const QString requestType = reply->property("requestType").toString();
    const QString clientId = reply->property("clientId").toString();
    const QString remark = reply->property("remark").toString();
    
    if (reply->error() != QNetworkReply::NoError) {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString errorMsg = reply->errorString();
        
        // 提供更友好的错误信息
        if (statusCode >= 400) {
            if (statusCode == 404) {
                errorMsg = tr("资源未找�?(404)");
            } else if (statusCode == 500) {
                errorMsg = tr("服务器错�?(500)");
            } else if (statusCode == 503) {
                errorMsg = tr("服务不可�?(503)");
            } else {
                errorMsg = tr("HTTP %1: %2").arg(statusCode).arg(errorMsg);
            }
        } else if (reply->error() == QNetworkReply::TimeoutError) {
            errorMsg = tr("请求超时，请检查网络连�?);
        } else if (reply->error() == QNetworkReply::ConnectionRefusedError) {
            errorMsg = tr("连接被拒绝，请检查服务是否运�?);
        }
        
        qWarning() << "[Console] REST API error:" << errorMsg << "for" << requestType;
        if (requestType == QStringLiteral("saveRemark")) {
            QMessageBox::warning(this, tr("保存备注失败"), 
                tr("无法保存备注到服务器�?1").arg(errorMsg));
        } else if (requestType == QStringLiteral("fetchClients")) {
            // 客户端列表获取失败时不弹窗，只在日志中记�?            qWarning() << "[Console] Failed to fetch clients from REST API:" << errorMsg;
        }
        reply->deleteLater();
        return;
    }
    
    if (requestType == QStringLiteral("saveRemark")) {
        const QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            const QString status = obj.value(QStringLiteral("status")).toString();
            if (status == QStringLiteral("ok")) {
                qInfo() << "[Console] Remark saved successfully for" << clientId;
                // Update local cache
                if (remark.isEmpty()) {
                    clientRemarksCache_.remove(clientId);
                } else {
                    clientRemarksCache_.insert(clientId, remark);
                }
                // Update UI
                auto it = clientEntries_.find(clientId);
                if (it != clientEntries_.end()) {
                    it->remark = remark;
                    updateClientTreeItem(clientId);
                    scheduleClientTreeRebuild();  // 批量更新
                    updateTileDisplayName(clientId);
                }
                // 纯UDP架构：不再需要从 REST API 同步
                // fetchClientsFromRestApi();
            } else {
                qWarning() << "[Console] Failed to save remark:" << obj.value(QStringLiteral("message")).toString();
            }
        }
    } else if (requestType == QStringLiteral("fetchClients")) {
        const QByteArray data = reply->readAll();
        if (data.isEmpty()) {
            qWarning() << "[Console] Empty response from fetchClients API";
            reply->deleteLater();
            return;
        }
        
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "[Console] Failed to parse clients JSON:" << parseError.errorString();
            reply->deleteLater();
            return;
        }
        
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            QJsonArray clients;
            if (obj.contains(QStringLiteral("clients")) && obj.value(QStringLiteral("clients")).isArray()) {
                clients = obj.value(QStringLiteral("clients")).toArray();
            } else if (doc.isArray()) {
                // 如果响应直接是数�?                clients = doc.array();
            }
            
            // Update remarks cache from server (merge with existing entries)
            for (const QJsonValue& value : clients) {
                const QJsonObject clientObj = value.toObject();
                const QString id = clientObj.value(QStringLiteral("client_id")).toString();
                if (id.isEmpty()) {
                    continue;
                }
                
                const QString serverRemark = clientObj.value(QStringLiteral("remark")).toString();
                if (!serverRemark.isEmpty()) {
                    clientRemarksCache_.insert(id, serverRemark);
                }
                
                // Update existing entry if present
                auto it = clientEntries_.find(id);
                if (it != clientEntries_.end()) {
                    it->remark = clientRemarksCache_.value(id, it->remark);
                }
            }
            
            // Update UI if we have client entries
            if (!clientEntries_.isEmpty()) {
                scheduleClientTreeRebuild();  // 批量更新
                // updateWallHeaderStats() 会在定时器中自动更新，不需要立即调�?            }
        } else if (doc.isArray()) {
            // 如果响应直接是数�?            qWarning() << "[Console] fetchClients returned array instead of object, skipping";
        }
    }
    
    reply->deleteLater();
}

void MainWindow::fetchClientsFromRestApi() {
    // 纯UDP架构：不再使�?REST API，客户端信息通过 UDP 心跳和数据库查询获取
    // CommandController 已集成到 DesktopConsole，无需外部 REST 调用
    return;
}

// ============================================================================
// 集成 CommandController 功能 (纯UDP架构)
// ============================================================================

void MainWindow::handleUdpDatagram() {
    while (udpReceiver_ && udpReceiver_->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpReceiver_->receiveDatagram();
        const QByteArray data = datagram.data();
        const QHostAddress sender = datagram.senderAddress();
        const quint16 senderPort = datagram.senderPort();
        
        qDebug() << "[Console] UDP datagram received from" << sender.toString() << ":" << senderPort << "size:" << data.size();
        
        // 尝试解析JSON消息
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(data, &error);
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject obj = doc.object();
            const QString type = obj.value(QStringLiteral("type")).toString();
            qDebug() << "[Console] UDP message type:" << type;
            
            if (type == QStringLiteral("heartbeat")) {
                handleHeartbeat(obj, sender, senderPort);
            } else if (type == QStringLiteral("activities")) {
                handleActivities(obj);
            } else if (type == QStringLiteral("app_usage")) {
                handleAppUsage(obj);
            } else if (type == QStringLiteral("window_change")) {
                handleWindowChange(obj);
            } else if (type == QStringLiteral("request_sensitive_words")) {
                const QString clientId = obj.value(QStringLiteral("client_id")).toString();
                sendSensitiveWordsUpdate(clientId, sender, senderPort);
            }
            return;
        }
        
        // 尝试解析二进制消�?(alert + screenshot)
        if (data.size() >= 10) {
            handleAlert(data);
        }
    }
}

void MainWindow::handleHeartbeat(const QJsonObject& obj, const QHostAddress& sender, quint16 port) {
    const QString clientId = obj.value(QStringLiteral("client_id")).toString();
    if (clientId.isEmpty()) {
        qWarning() << "[Console] Received heartbeat with empty client_id";
        return;
    }
    
    // 提取 SSRC（用于视频流匹配�?    // 注意: 使用 toString + toULongLong 避免 double 精度丢失
    bool ok = false;
    const quint32 ssrc = static_cast<quint32>(
        obj.value(QStringLiteral("ssrc")).toString().toULongLong(&ok)
    );
    if (!ok && obj.contains(QStringLiteral("ssrc"))) {
        qWarning() << "[Console] Failed to parse SSRC from heartbeat";
    }
    
    // 记录心跳时间
    clientLastHeartbeat_[clientId] = QDateTime::currentDateTimeUtc();
    qInfo() << "[Console] Heartbeat received from" << clientId << "at" << sender.toString() << ":" << port
            << "SSRC:" << ssrc;
    
    // 确保客户端条目存�?    auto it = clientEntries_.find(clientId);
    if (it == clientEntries_.end()) {
        // 新客户端，创建条�?        ClientEntry entry;
        entry.id = clientId;
        entry.hostname = obj.value(QStringLiteral("hostname")).toString(clientId);
        entry.username = obj.value(QStringLiteral("username")).toString();
        entry.ip = sender.toString();
        entry.ssrc = ssrc;  // 保存 SSRC
        entry.online = true;
        entry.lastSeen = QDateTime::currentDateTimeUtc();
        clientEntries_.insert(clientId, entry);
        qInfo() << "[Console] New client registered:" << clientId << "from" << sender.toString()
                << "SSRC:" << ssrc;
        
        // UDP模式: 立即创建 StreamTile (不需要StreamPlayer)
        if (!activeTiles_.contains(clientId) && ssrc != 0) {
            qInfo() << "[Console] Creating UDP video tile for" << clientId << "SSRC:" << ssrc;
            auto* tile = new StreamTile(clientId, ssrc, previewContainer_);
            connect(tile, &StreamTile::aspectRatioChanged, this, [this](const QString&) {
                schedulePreviewRelayout();
            });
            connect(tile, &StreamTile::contextMenuRequested, this, &MainWindow::handleTileContextMenu);
            connect(tile, &StreamTile::tileDoubleClicked, this, &MainWindow::openFullscreenView);
            activeTiles_.insert(clientId, tile);
            qDebug() << "[Console] �?Tile inserted into activeTiles_, size now:" << activeTiles_.size();
            tile->setDragEnabled(!layoutLocked_ && !wallFullscreen_);
            
            // 设置 UDP 连接状�?            QString statusText = QString("UDP | Port: 5004 | SSRC: %1").arg(QString::number(ssrc));
            tile->setErrorMessage(statusText);
            
            tile->show();
            
            if (!layoutOrder_.contains(clientId)) {
                layoutOrder_.append(clientId);
            }
            schedulePreviewRelayout();
        }
        
        scheduleClientTreeRebuild();
    } else {
        // 现有客户端，更新状态和 SSRC
        if (ssrc != 0) {
            it->ssrc = ssrc;
        }
        it->online = true;
        it->lastSeen = QDateTime::currentDateTimeUtc();
        scheduleClientTreeRebuild();
    }
    
    // 回复 heartbeat_ack
    sendHeartbeatAck(clientId, sender, port);
    qInfo() << "[Console] Heartbeat ACK sent to" << clientId;
}

void MainWindow::sendHeartbeatAck(const QString& clientId, const QHostAddress& address, quint16 port) {
    QJsonObject ack;
    ack[QStringLiteral("type")] = QStringLiteral("heartbeat_ack");
    ack[QStringLiteral("client_id")] = clientId;
    ack[QStringLiteral("timestamp")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    sendUdpMessage(ack, address, port);
}

void MainWindow::sendUdpMessage(const QJsonObject& message, const QHostAddress& address, quint16 port) {
    if (!udpReceiver_) return;
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    udpReceiver_->writeDatagram(payload, address, port);
}

void MainWindow::checkClientHeartbeats() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const qint64 timeout = 90000;  // 90秒超�?    
    for (auto it = clientEntries_.begin(); it != clientEntries_.end(); ++it) {
        const QString& clientId = it.key();
        if (!clientLastHeartbeat_.contains(clientId)) {
            continue;
        }
        
        const qint64 elapsed = clientLastHeartbeat_[clientId].msecsTo(now);
        if (elapsed > timeout && it->online) {
            it->online = false;
            qWarning() << "[Console] Client" << clientId << "heartbeat timeout (offline)";
            scheduleClientTreeRebuild();
        }
    }
}

void MainWindow::handleAlert(const QByteArray& data) {
    // 解析二进制协�? type(4) + clientIdLen(2) + clientId + metadataLen(4) + metadata + screenshot
    if (data.size() < 10) return;
    
    const quint32 type = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(data.constData()));
    const quint16 clientIdLen = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(data.constData() + 4));
    if (data.size() < 6 + clientIdLen + 4) return;
    
    const QString clientId = QString::fromUtf8(data.mid(6, clientIdLen));
    int offset = 6 + clientIdLen;
    
    const quint32 metadataLen = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(data.constData() + offset));
    offset += 4;
    if (data.size() < offset + static_cast<int>(metadataLen)) return;
    
    QJsonObject metadata;
    if (metadataLen > 0) {
        const QJsonDocument metaDoc = QJsonDocument::fromJson(data.mid(offset, metadataLen));
        if (metaDoc.isObject()) {
            metadata = metaDoc.object();
        }
    }
    offset += metadataLen;
    
    const QByteArray screenshotData = data.mid(offset);
    
    // 保存到数据库
    insertAlertRecord(clientId, metadata);
    
    // 保存截图
    const QString timestamp = metadata.value(QStringLiteral("timestamp")).toString();
    QString savedPath = saveScreenshotFileDirect(clientId, screenshotData, timestamp, true);
    
    qInfo() << "[Console] Alert received from" << clientId << "screenshot:" << savedPath;
    
    // 更新UI (通过现有机制)
    if (clientEntries_.contains(clientId)) {
        scheduleClientTreeRebuild();
    }
}

void MainWindow::handleActivities(const QJsonObject& obj) {
    const QString clientId = obj.value(QStringLiteral("client_id")).toString();
    const QJsonArray activities = obj.value(QStringLiteral("activities")).toArray();
    
    if (clientId.isEmpty() || activities.isEmpty()) return;
    
    // 存储到内�?(保持现有机制)
    clientActivitiesData_[clientId] = activities;
    
    // 保存到数据库
    if (ensureDatabase()) {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "INSERT INTO activity_logs (client_id, activity_type, data, timestamp) "
            "VALUES (:client_id, :type, :data, :timestamp)"));
        
        for (const QJsonValue& value : activities) {
            const QJsonObject activity = value.toObject();
            query.bindValue(QStringLiteral(":client_id"), clientId);
            query.bindValue(QStringLiteral(":type"), activity.value(QStringLiteral("activity_type")).toString());
            query.bindValue(QStringLiteral(":data"), QJsonDocument(activity).toJson(QJsonDocument::Compact));
            query.bindValue(QStringLiteral(":timestamp"), activity.value(QStringLiteral("timestamp")).toString());
            query.exec();
        }
    }
}

void MainWindow::handleAppUsage(const QJsonObject& obj) {
    const QString clientId = obj.value(QStringLiteral("client_id")).toString();
    const QJsonArray usage = obj.value(QStringLiteral("app_usage")).toArray();
    
    if (clientId.isEmpty() || usage.isEmpty()) return;
    
    // 存储到内�?    clientAppUsageData_[clientId] = usage;
}

void MainWindow::handleWindowChange(const QJsonObject& obj) {
    const QString clientId = obj.value(QStringLiteral("client_id")).toString();
    const QString windowTitle = obj.value(QStringLiteral("window_title")).toString();
    const QString appName = obj.value(QStringLiteral("app_name")).toString();
    
    // 记录活动日志
    if (ensureDatabase()) {
        QSqlQuery query(db_);
        query.prepare(QStringLiteral(
            "INSERT INTO activity_logs (client_id, activity_type, data, timestamp) "
            "VALUES (:client_id, 'window_change', :data, :timestamp)"));
        query.bindValue(QStringLiteral(":client_id"), clientId);
        QJsonObject data;
        data[QStringLiteral("window_title")] = windowTitle;
        data[QStringLiteral("app_name")] = appName;
        query.bindValue(QStringLiteral(":data"), QJsonDocument(data).toJson(QJsonDocument::Compact));
        query.bindValue(QStringLiteral(":timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        query.exec();
    }
}

void MainWindow::insertAlertRecord(const QString& clientId, const QJsonObject& alertObj) {
    if (!ensureDatabase()) return;
    
    const QString keyword = alertObj.value(QStringLiteral("word")).toString();
    const QString windowTitle = alertObj.value(QStringLiteral("window_title")).toString();
    const QString context = alertObj.value(QStringLiteral("context")).toString();
    const QString timestamp = alertObj.value(QStringLiteral("timestamp")).toString();
    const QString alertType = alertObj.value(QStringLiteral("alert_type")).toString(QStringLiteral("sensitive_word"));
    
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "INSERT INTO alerts (client_id, alert_type, keyword, window_title, context, timestamp) "
        "VALUES (:client_id, :alert_type, :keyword, :window_title, :context, :timestamp)"));
    query.bindValue(QStringLiteral(":client_id"), clientId);
    query.bindValue(QStringLiteral(":alert_type"), alertType);
    query.bindValue(QStringLiteral(":keyword"), keyword);
    query.bindValue(QStringLiteral(":window_title"), windowTitle);
    query.bindValue(QStringLiteral(":context"), context);
    query.bindValue(QStringLiteral(":timestamp"), timestamp);
    
    if (!query.exec()) {
        qWarning() << "[Console] Failed to insert alert:" << query.lastError().text();
    }
}

void MainWindow::updateClientRecord(const QString& clientId, const QString& hostname,
                                     const QString& ipAddress, const QString& osInfo,
                                     const QString& username, const QString& status) {
    if (!ensureDatabase()) return;
    
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO clients (client_id, hostname, ip_address, os_info, username, last_seen, status) "
        "VALUES (:client_id, :hostname, :ip_address, :os_info, :username, :last_seen, :status)"));
    query.bindValue(QStringLiteral(":client_id"), clientId);
    query.bindValue(QStringLiteral(":hostname"), hostname);
    query.bindValue(QStringLiteral(":ip_address"), ipAddress);
    query.bindValue(QStringLiteral(":os_info"), osInfo);
    query.bindValue(QStringLiteral(":username"), username);
    query.bindValue(QStringLiteral(":last_seen"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.bindValue(QStringLiteral(":status"), status);
    query.exec();
}

QStringList MainWindow::loadSensitiveWords() {
    QStringList words;
    
    // 从数据库加载
    if (ensureDatabase()) {
        QSqlQuery query(db_);
        if (query.exec(QStringLiteral("SELECT word FROM sensitive_words ORDER BY word"))) {
            while (query.next()) {
                words.append(query.value(0).toString());
            }
        }
    }
    
    // 如果数据库为�?从文件加�?    if (words.isEmpty()) {
        const QString filePath = QCoreApplication::applicationDirPath() + QStringLiteral("/config/sensitive_words.json");
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) {
                for (const QJsonValue& value : doc.array()) {
                    if (value.isString()) {
                        words.append(value.toString());
                    }
                }
            }
        }
    }
    
    return words;
}

void MainWindow::sendSensitiveWordsUpdate(const QString& clientId, const QHostAddress& address, quint16 port) {
    QJsonObject message;
    message[QStringLiteral("type")] = QStringLiteral("sensitive_words_update");
    message[QStringLiteral("client_id")] = clientId;
    QJsonArray wordsArray;
    for (const QString& word : sensitiveWords_) {
        wordsArray.append(word);
    }
    message[QStringLiteral("words")] = wordsArray;
    sendUdpMessage(message, address, port);
}

void MainWindow::broadcastSensitiveWordsUpdateViaUdp() {
    // 广播给所有在线客户端 (通过最近心跳的地址)
    // TODO: 实现地址缓存机制
}

// ============================================================================
// 视频流处�?(纯UDP架构)
// ============================================================================

void MainWindow::handleVideoFrame(quint32 ssrc, quint32 frameId, const QByteArray& jpegData) {
    // 根据 SSRC 查找对应的客户端ID
    QString clientId = findClientBySSRC(ssrc);
    if (clientId.isEmpty()) {
        // 未知�?SSRC，记录但不处�?        static QSet<quint32> unknownSSRCs;
        if (!unknownSSRCs.contains(ssrc)) {
            qDebug() << "[Console] Received video from unknown SSRC:" << ssrc;
            unknownSSRCs.insert(ssrc);
        }
        return;
    }
    
    // 更新视频Tile
    updateVideoTile(clientId, jpegData);
    
    // 更新 Tile 显示: UDP 连接状�?    auto tileIt = activeTiles_.find(clientId);
    if (tileIt != activeTiles_.end() && tileIt.value()) {
        // 显示 UDP 连接参数：端口、SSRC、帧�?
        QString statusText = QString("UDP:5004 | SSRC:%1 | Frame:%2")
            .arg(QString::number(ssrc))
            .arg(frameId);
        tileIt.value()->setErrorMessage(statusText);
    }
}

void MainWindow::updateVideoTile(const QString& clientId, const QByteArray& jpegData) {
    // 查找对应�?StreamTile
    auto tileIt = activeTiles_.find(clientId);
    if (tileIt == activeTiles_.end()) {
        // Tile 不存在，但不要重复创�?
        // 心跳时已经创建，如果这里还是找不到说明有其他问题
        static QSet<QString> reportedMissing;
        if (!reportedMissing.contains(clientId)) {
            qWarning() << "[Console] Tile not found for client:" << clientId << "(activeTiles size:" << activeTiles_.size() << ")";
            reportedMissing.insert(clientId);
        }
        return;
    }
    
    StreamTile* tile = tileIt.value();
    if (!tile) {
        return;
    }
    
    // 解码 JPEG 并显�?    QImage image;
    if (!image.loadFromData(jpegData, "JPEG")) {
        qWarning() << "[Console] Failed to decode JPEG for client:" << clientId;
        return;
    }
    
    qDebug() << "[Console] �?JPEG decoded!" << clientId << "size:" << image.width() << "x" << image.height();
    
    // 更新 Tile 显示（使用已有的 setFrame 方法�?    tile->setFrame(image);
    qDebug() << "[Console] �?setFrame called for" << clientId;
    
    // 同步更新全屏窗口（如果打开�?
    if (activeFullscreen_ && activeFullscreen_->clientId() == clientId) {
        activeFullscreen_->setFrame(image);
    }
}

QString MainWindow::findClientBySSRC(quint32 ssrc) const {
    // SSRC 存储�?ClientEntry �?    for (auto it = clientEntries_.constBegin(); it != clientEntries_.constEnd(); ++it) {
        if (it->ssrc == ssrc) {
            return it.key();
        }
    }
    return QString();
}

}  // namespace console

#include "main_window.moc"
