#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include "console/main_window.hpp"

namespace {
QFile gLogFile;
QMutex gLogMutex;

QString severityPrefix(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("[DEBUG] ");
    case QtInfoMsg:
        return QStringLiteral("[INFO ] ");
    case QtWarningMsg:
        return QStringLiteral("[WARN ] ");
    case QtCriticalMsg:
        return QStringLiteral("[ERROR] ");
    case QtFatalMsg:
        return QStringLiteral("[FATAL] ");
    }
    return QStringLiteral("[UNKWN] ");
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    Q_UNUSED(context);
    const QString prefix = severityPrefix(type);
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz "));
    const QString line = timestamp + prefix + msg + QLatin1Char('\n');

    {
        QMutexLocker locker(&gLogMutex);
        if (gLogFile.isOpen()) {
            QTextStream stream(&gLogFile);
            stream << line;
            stream.flush();
        }
    }

    fprintf(stderr, "%s", line.toLocal8Bit().constData());
    fflush(stderr);

    if (type == QtFatalMsg) {
        abort();
    }
}

void setupLogging() {
    const QString logDirPath = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
    QDir dir;
    dir.mkpath(logDirPath);
    const QString logPath = logDirPath + QStringLiteral("/desktop_console.log");

    // 清除旧日志文件，避免日志冗余和沉淀
    if (QFile::exists(logPath)) {
        QFile::remove(logPath);
    }

    gLogFile.setFileName(logPath);
    if (!gLogFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        fprintf(stderr, "Failed to open log file: %s\n", logPath.toLocal8Bit().constData());
    }

    qInstallMessageHandler(messageHandler);
    qInfo() << "Logging initialized ->" << logPath;
}
}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    setupLogging();
    
    // 抑制FFmpeg的日志输出，避免H.264/H.265解码错误信息干扰（我们使用JPEG进行直播流）
    // AV_LOG_ERROR = 16，只显示错误，不显示警告和信息
    // 注意：此调用需要在运行时 FFmpeg 库已加载后才能工作
    // 如果编译时链接问题，可以在 main_window.cpp 中调用
    // av_log_set_level(16);

    console::MainWindow window;
    window.show();

    return app.exec();
}
