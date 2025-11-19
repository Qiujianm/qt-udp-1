// 数据库集成实现 (从 CommandController 移植)
#include "console/main_window.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

namespace {

QString normalizedPath(const QString& path) {
    if (path.isEmpty()) {
        return QString();
    }
    return QDir(path).absolutePath();
}

QStringList buildDataDirCandidates(const QString& appDir) {
    QStringList candidates;
    auto appendCandidate = [&candidates](const QString& path) {
        const QString normalized = normalizedPath(path);
        if (normalized.isEmpty()) {
            return;
        }
        if (!candidates.contains(normalized)) {
            candidates.append(normalized);
        }
    };

    const QString envData = qEnvironmentVariable("MONITOR_DATA_DIR");
    appendCandidate(envData);

    const QString appData = QDir(appDir).filePath(QStringLiteral("data"));
    appendCandidate(appData);
    appendCandidate(QDir(appDir).filePath(QStringLiteral("../data")));
    appendCandidate(QDir(appDir).filePath(QStringLiteral("../server/data")));

    QDir walker(appDir);
    for (int i = 0; i < 8; ++i) {
        appendCandidate(walker.absoluteFilePath(QStringLiteral("data")));
        appendCandidate(walker.absoluteFilePath(QStringLiteral("server/data")));
        if (!walker.cdUp()) {
            break;
        }
    }

    return candidates;
}

}  // namespace

namespace console {

bool MainWindow::initDatabase() {
    if (databaseInitialized_) {
        return ensureDatabase();
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    QString chosenDir;
    QString chosenDbPath;

    const QString envDbPath = qEnvironmentVariable("MONITOR_DB_PATH");
    if (!envDbPath.isEmpty()) {
        const QFileInfo envDbInfo(envDbPath);
        if (envDbInfo.exists() && envDbInfo.isFile()) {
            chosenDir = envDbInfo.dir().absolutePath();
            chosenDbPath = envDbInfo.absoluteFilePath();
        }
    }

    if (chosenDbPath.isEmpty()) {
        const QStringList candidates = buildDataDirCandidates(appDir);
        for (const QString& dir : candidates) {
            const QString dbCandidate = QDir(dir).filePath(QStringLiteral("monitor.db"));
            if (QFileInfo::exists(dbCandidate)) {
                chosenDir = dir;
                chosenDbPath = QFileInfo(dbCandidate).absoluteFilePath();
                break;
            }
        }
    }

    if (chosenDbPath.isEmpty()) {
        const QStringList candidates = buildDataDirCandidates(appDir);
        if (!candidates.isEmpty()) {
            chosenDir = candidates.first();
        } else {
            chosenDir = QDir(appDir).filePath(QStringLiteral("data"));
        }
        if (!QDir().mkpath(chosenDir)) {
            qCritical() << "[Console] Failed to create data directory:" << chosenDir;
            return false;
        }
        chosenDbPath = QDir(chosenDir).filePath(QStringLiteral("monitor.db"));
    }

    dataDir_ = chosenDir;
    dbPath_ = chosenDbPath;
    screenshotDir_ = QDir(dataDir_).filePath("screenshots");
    alertsDir_ = QDir(screenshotDir_).filePath("alerts");
    QDir().mkpath(screenshotDir_);
    QDir().mkpath(alertsDir_);

    qInfo() << "[Console] Integrated database path:" << dbPath_;

    if (!QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE"))) {
        qCritical() << "[Console] QSQLITE driver not available!";
        return false;
    }

    dbConnectionName_ = QStringLiteral("console_integrated_db");
    if (QSqlDatabase::contains(dbConnectionName_)) {
        db_ = QSqlDatabase::database(dbConnectionName_);
    } else {
        db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), dbConnectionName_);
    }

    db_.setDatabaseName(dbPath_);
    if (!db_.open()) {
        qCritical() << "[Console] Unable to open database:" << db_.lastError().text();
        return false;
    }

    QSqlQuery query(db_);
    query.exec(QStringLiteral("PRAGMA journal_mode=WAL"));

    // 创建表结构
    query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS clients ("
        "client_id TEXT PRIMARY KEY,"
        "hostname TEXT,"
        "ip_address TEXT,"
        "os_info TEXT,"
        "username TEXT,"
        "last_seen TEXT,"
        "status TEXT,"
        "telegram_chat_id TEXT)"));

    query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS activity_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "client_id TEXT,"
        "activity_type TEXT,"
        "data TEXT,"
        "timestamp TEXT)"));

    query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS screenshots ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "client_id TEXT,"
        "file_path TEXT,"
        "timestamp TEXT)"));

    query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS alerts ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "client_id TEXT,"
        "alert_type TEXT,"
        "keyword TEXT,"
        "window_title TEXT,"
        "context TEXT,"
        "timestamp TEXT,"
        "screenshot TEXT)"));

    query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS app_usage ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "client_id TEXT,"
        "app_name TEXT,"
        "total_seconds INTEGER,"
        "timestamp TEXT)"));

    query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS sensitive_words ("
        "word TEXT PRIMARY KEY,"
        "created_at TEXT)"));

    databaseInitialized_ = true;
    qInfo() << "[Console] Database initialized successfully";
    return true;
}

bool MainWindow::ensureDatabase() {
    if (!databaseInitialized_) {
        return initDatabase();
    }
    if (!db_.isValid()) {
        databaseInitialized_ = false;
        return initDatabase();
    }
    if (!db_.isOpen()) {
        if (!db_.open()) {
            qCritical() << "[Console] Failed to reopen database:" << db_.lastError();
            return false;
        }
    }
    return true;
}

QString MainWindow::saveScreenshotFileDirect(const QString& clientId, const QByteArray& data,
                                             const QString& timestamp, bool isAlert, QString* isoTimestampOut) {
    const QString targetDir = isAlert ? alertsDir_ : screenshotDir_;
    const QString clientDir = QDir(targetDir).filePath(clientId);
    QDir().mkpath(clientDir);

    QString mutableTimestamp = timestamp.isEmpty() ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate) : timestamp;
    const QString filename = mutableTimestamp.replace(':', '-') + QStringLiteral(".jpg");
    const QString filePath = QDir(clientDir).filePath(filename);

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();
        if (isoTimestampOut) {
            *isoTimestampOut = mutableTimestamp;
        }
        return filePath;
    }

    return QString();
}

}  // namespace console
