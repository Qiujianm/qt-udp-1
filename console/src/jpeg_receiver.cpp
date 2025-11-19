#include "console/jpeg_receiver.hpp"
#include <QDebug>
#include <QDateTime>
#include <QtEndian>

namespace console {

namespace {
constexpr quint32 kMagic = 0x4a503031;  // "JP01"
constexpr int kHeaderSize = 20;
}

JpegReceiver::JpegReceiver(quint16 port, QObject* parent)
    : QObject(parent), port_(port) {
    
    cleanupTimer_ = new QTimer(this);
    cleanupTimer_->setInterval(1000);  // 每秒清理一次超时帧
    connect(cleanupTimer_, &QTimer::timeout, this, &JpegReceiver::cleanupOldFrames);
}

JpegReceiver::~JpegReceiver() {
    stop();
}

bool JpegReceiver::start() {
    if (socket_ && socket_->isValid()) {
        qWarning() << "[JpegReceiver] Already running";
        return true;
    }
    
    socket_ = new QUdpSocket(this);
    
    // 绑定 UDP 端口
    if (!socket_->bind(QHostAddress::Any, port_, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning() << "[JpegReceiver] Failed to bind UDP port" << port_ << ":" << socket_->errorString();
        delete socket_;
        socket_ = nullptr;
        return false;
    }
    
    // 优化：增大接收缓冲区
    socket_->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 16 * 1024 * 1024);  // 16MB
    
    connect(socket_, &QUdpSocket::readyRead, this, &JpegReceiver::handlePendingDatagrams);
    
    cleanupTimer_->start();
    
    qInfo() << "[JpegReceiver] Started listening on UDP port" << port_;
    return true;
}

void JpegReceiver::stop() {
    if (cleanupTimer_) {
        cleanupTimer_->stop();
    }
    
    if (socket_) {
        socket_->close();
        delete socket_;
        socket_ = nullptr;
    }
    
    assemblyBuffers_.clear();
    stats_.clear();
    lastFrameTime_.clear();
    
    qInfo() << "[JpegReceiver] Stopped";
}

void JpegReceiver::handlePendingDatagrams() {
    while (socket_ && socket_->hasPendingDatagrams()) {
        QNetworkDatagram datagram = socket_->receiveDatagram();
        const QByteArray data = datagram.data();
        
        if (data.size() < kHeaderSize) {
            continue;  // 数据包太小
        }
        
        JP01Header header;
        if (!parseHeader(data, header)) {
            continue;  // 无效的头部
        }
        
        // 提取 payload
        const QByteArray payload = data.mid(kHeaderSize);
        if (payload.size() != header.payloadSize) {
            qDebug() << "[JpegReceiver] Payload size mismatch:" << payload.size() << "!=" << header.payloadSize;
            continue;
        }
        
        // 更新统计
        stats_[header.ssrc].fragmentsReceived++;
        
        // 处理分片
        processFragment(header, payload);
    }
}

bool JpegReceiver::parseHeader(const QByteArray& datagram, JP01Header& header) {
    if (datagram.size() < kHeaderSize) {
        return false;
    }
    
    const uchar* data = reinterpret_cast<const uchar*>(datagram.constData());
    
    header.magic = qFromBigEndian<quint32>(data);
    if (header.magic != kMagic) {
        return false;  // 无效的 magic
    }
    
    header.ssrc = qFromBigEndian<quint32>(data + 4);
    header.frameId = qFromBigEndian<quint32>(data + 8);
    header.fragmentIndex = qFromBigEndian<quint16>(data + 12);
    header.fragmentCount = qFromBigEndian<quint16>(data + 14);
    header.payloadSize = qFromBigEndian<quint16>(data + 16);
    header.reserved = qFromBigEndian<quint16>(data + 18);
    
    return true;
}

void JpegReceiver::processFragment(const JP01Header& header, const QByteArray& payload) {
    auto& frameBuffer = assemblyBuffers_[header.ssrc];
    
    // 检查缓冲区大小，防止内存溢出
    if (frameBuffer.size() > kMaxPendingFrames) {
        // 移除最旧的帧
        auto oldestIt = frameBuffer.begin();
        qint64 oldestTime = oldestIt->firstFragmentTime;
        for (auto it = frameBuffer.begin(); it != frameBuffer.end(); ++it) {
            if (it->firstFragmentTime < oldestTime) {
                oldestTime = it->firstFragmentTime;
                oldestIt = it;
            }
        }
        stats_[header.ssrc].framesDropped++;
        frameBuffer.erase(oldestIt);
    }
    
    // 获取或创建帧组装结构
    FrameAssembly& assembly = frameBuffer[header.frameId];
    
    // 初始化新帧
    if (assembly.totalFragments == 0) {
        assembly.frameId = header.frameId;
        assembly.totalFragments = header.fragmentCount;
        assembly.receivedFragments = 0;
        assembly.firstFragmentTime = QDateTime::currentMSecsSinceEpoch();
        
        // 预分配空间（估算每个分片约 1400 字节）
        assembly.data.reserve(header.fragmentCount * 1400);
    }
    
    // 检查分片是否已接收
    if (assembly.receivedMap.contains(header.fragmentIndex)) {
        return;  // 重复分片
    }
    
    // 记录分片
    assembly.receivedMap[header.fragmentIndex] = true;
    assembly.receivedFragments++;
    
    // 按顺序写入数据（如果分片乱序，需要重新排列）
    // 为简化实现，这里假设分片按顺序到达，实际使用时可能需要缓冲
    assembly.data.append(payload);
    
    // 检查是否收集齐所有分片
    if (assembly.receivedFragments == assembly.totalFragments) {
        assembleFrame(header.ssrc, assembly);
        frameBuffer.remove(header.frameId);
    }
}

void JpegReceiver::assembleFrame(quint32 ssrc, FrameAssembly& assembly) {
    // 分片可能乱序，需要重新排列
    QByteArray orderedData;
    orderedData.reserve(assembly.data.size());
    
    // 简化实现：假设 assembly.data 已经按顺序（实际上可能需要排序）
    // 完整实现需要维护分片索引->数据的映射
    orderedData = assembly.data;
    
    // 更新统计
    stats_[ssrc].framesReceived++;
    
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastFrameTime_.contains(ssrc)) {
        qint64 elapsed = now - lastFrameTime_[ssrc];
        if (elapsed > 0) {
            double instantFps = 1000.0 / elapsed;
            stats_[ssrc].avgFps = stats_[ssrc].avgFps * 0.9 + instantFps * 0.1;  // 指数移动平均
        }
    }
    lastFrameTime_[ssrc] = now;
    
    qDebug() << "[JpegReceiver] Frame assembled: SSRC" << ssrc 
             << "Frame" << assembly.frameId
             << "Size:" << orderedData.size() << "bytes"
             << "Fragments:" << assembly.totalFragments
             << "FPS:" << QString::number(stats_[ssrc].avgFps, 'f', 2);
    
    emit frameReceived(ssrc, assembly.frameId, orderedData);
}

void JpegReceiver::cleanupOldFrames() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    
    for (auto ssrcIt = assemblyBuffers_.begin(); ssrcIt != assemblyBuffers_.end(); ++ssrcIt) {
        auto& frameBuffer = ssrcIt.value();
        
        for (auto it = frameBuffer.begin(); it != frameBuffer.end(); ) {
            if (now - it->firstFragmentTime > kFrameTimeoutMs) {
                // 超时，计算丢失的分片数
                quint16 lostFragments = it->totalFragments - it->receivedFragments;
                stats_[ssrcIt.key()].fragmentsLost += lostFragments;
                stats_[ssrcIt.key()].framesDropped++;
                
                qDebug() << "[JpegReceiver] Frame timeout: SSRC" << ssrcIt.key()
                         << "Frame" << it->frameId
                         << "Lost" << lostFragments << "fragments";
                
                it = frameBuffer.erase(it);
            } else {
                ++it;
            }
        }
    }
}

JpegReceiver::Stats JpegReceiver::getStats(quint32 ssrc) const {
    return stats_.value(ssrc);
}

}  // namespace console
