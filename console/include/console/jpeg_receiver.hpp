#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QMap>
#include <QByteArray>
#include <QTimer>
#include <QHostAddress>
#include <QImage>

namespace console {

// JP01 协议头部结构（20字节）
struct JP01Header {
    quint32 magic;           // 0x4a503031 "JP01"
    quint32 ssrc;            // 流标识
    quint32 frameId;         // 帧序号
    quint16 fragmentIndex;   // 分片索引
    quint16 fragmentCount;   // 总分片数
    quint16 payloadSize;     // 当前分片大小
    quint16 reserved;        // 保留
};

// 帧重组状态
struct FrameAssembly {
    quint32 frameId{0};
    quint16 totalFragments{0};
    quint16 receivedFragments{0};
    QByteArray data;
    QMap<quint16, bool> receivedMap;  // 记录已收到的分片
    qint64 firstFragmentTime{0};      // 首个分片到达时间（毫秒）
};

/**
 * @brief JPEG-over-UDP 接收器
 * 接收 StreamClient 通过 UDP 5004 发送的 JP01 协议视频流
 */
class JpegReceiver : public QObject {
    Q_OBJECT

public:
    explicit JpegReceiver(quint16 port = 5004, QObject* parent = nullptr);
    ~JpegReceiver() override;

    bool start();
    void stop();
    bool isRunning() const { return socket_ && socket_->isValid(); }
    
    // 统计信息
    struct Stats {
        quint32 framesReceived{0};
        quint32 framesDropped{0};
        quint32 fragmentsReceived{0};
        quint32 fragmentsLost{0};
        double avgFps{0.0};
    };
    Stats getStats(quint32 ssrc) const;

signals:
    void frameReceived(quint32 ssrc, quint32 frameId, const QByteArray& jpegData);
    void error(const QString& message);

private slots:
    void handlePendingDatagrams();
    void cleanupOldFrames();

private:
    bool parseHeader(const QByteArray& datagram, JP01Header& header);
    void processFragment(const JP01Header& header, const QByteArray& payload);
    void assembleFrame(quint32 ssrc, FrameAssembly& assembly);

    quint16 port_;
    QUdpSocket* socket_{nullptr};
    
    // 每个 SSRC 有独立的帧重组缓冲区（支持多客户端）
    QMap<quint32, QMap<quint32, FrameAssembly>> assemblyBuffers_;  // ssrc -> (frameId -> assembly)
    QMap<quint32, Stats> stats_;  // ssrc -> stats
    QMap<quint32, qint64> lastFrameTime_;  // ssrc -> timestamp (用于计算FPS)
    
    QTimer* cleanupTimer_{nullptr};
    static constexpr int kFrameTimeoutMs = 2000;  // 帧超时时间（2秒）
    static constexpr int kMaxPendingFrames = 10;   // 每个流最多保留10帧的缓冲
};

}  // namespace console
