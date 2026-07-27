#include "ZeroTierJoin.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <launch/LaunchTask.h>
#include "Application.h"
#include "FileSystem.h"

static const QString ZT_VERSION = "1.14.2";
static const QString ZT_API_BASE = "https://my.zerotier.com/api";

#ifdef Q_OS_WIN
static const QString ZT_CLI_NAME = "zerotier-cli.exe";
#elif defined(Q_OS_LINUX)
static const QString ZT_CLI_NAME = "zerotier-cli";
#else
static const QString ZT_CLI_NAME = "zerotier-cli";
#endif

ZeroTierJoin::ZeroTierJoin(LaunchTask* parent, const QString& networkId, const QString& apiToken)
    : LaunchStep(parent), m_networkId(networkId), m_apiToken(apiToken)
{
    connect(&m_process, &LoggedProcess::log, this, &ZeroTierJoin::logLines);
    connect(&m_process, &LoggedProcess::stateChanged, this, &ZeroTierJoin::on_state);

    m_ztDir = FS::PathCombine(APPLICATION->dataRoot(), "zerotier");
    FS::ensureFolderPathExists(m_ztDir);
}

QString ZeroTierJoin::findZerotierCli()
{
    // 1. Check bundled location
    QString bundled = FS::PathCombine(m_ztDir, ZT_CLI_NAME);
    if (QFileInfo::exists(bundled))
        return bundled;

    // 2. Check system PATH
    QString sysPath = QStandardPaths::findExecutable("zerotier-cli");
    if (!sysPath.isEmpty())
        return sysPath;

    // 3. Check common install locations
    QStringList candidates;
#ifdef Q_OS_WIN
    candidates << "C:\\Program Files\\ZeroTier\\ZeroTier One\\zerotier-cli.exe";
    candidates << "C:\\Program Files (x86)\\ZeroTier\\ZeroTier One\\zerotier-cli.exe";
#else
    candidates << "/usr/sbin/zerotier-cli"
               << "/usr/local/bin/zerotier-cli"
               << "/snap/bin/zerotier-cli";
#endif
    for (const auto& c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }
    return {};
}

void ZeroTierJoin::executeTask()
{
    m_ztPath = findZerotierCli();

    if (m_ztPath.isEmpty()) {
#ifdef Q_OS_WIN
        emit logLine(tr("ZeroTier not found. Starting ZeroTier service..."), MessageLevel::Launcher);
        QProcess svcCheck;
        svcCheck.start("sc", { "query", "ZeroTierOneService" });
        svcCheck.waitForFinished(5000);
        QString svcOutput = svcCheck.readAllStandardOutput();

        if (svcOutput.contains("RUNNING")) {
            emit logLine(tr("ZeroTier service is already running"), MessageLevel::Launcher);
            m_step = 2;
            joinNetwork();
            return;
        }

        QProcess svcStart;
        svcStart.start("net", { "start", "ZeroTierOneService" });
        svcStart.waitForFinished(10000);

        if (svcStart.exitCode() == 0) {
            emit logLine(tr("ZeroTier service started successfully"), MessageLevel::Launcher);
            m_ztPath = FS::PathCombine("C:\\Program Files\\ZeroTier\\ZeroTier One", ZT_CLI_NAME);
            if (!QFileInfo::exists(m_ztPath))
                m_ztPath = FS::PathCombine("C:\\Program Files (x86)\\ZeroTier\\ZeroTier One", ZT_CLI_NAME);
            if (QFileInfo::exists(m_ztPath)) {
                m_step = 2;
                joinNetwork();
                return;
            }
        }
#endif
        emit logLine(tr("ZeroTier not found. Continuing without ZeroTier..."), MessageLevel::Warning);
        emitSucceeded();
        return;
    }

    emit logLine(tr("ZeroTier found at: %1").arg(m_ztPath), MessageLevel::Launcher);

    // Check if already joined
    QProcess checkProcess;
    checkProcess.start(m_ztPath, { "listnetworks" });
    checkProcess.waitForFinished(5000);
    QString output = checkProcess.readAllStandardOutput();
    if (output.contains(m_networkId)) {
        emit logLine(tr("Already connected to network %1").arg(m_networkId), MessageLevel::Launcher);
        // Still try to authorize
        getLocalNodeId();
        return;
    }

    m_step = 1;
    ensureServiceRunning();
}

void ZeroTierJoin::ensureServiceRunning()
{
#ifdef Q_OS_LINUX
    QProcess statusProc;
    statusProc.start("systemctl", { "is-active", "zerotier-one" });
    statusProc.waitForFinished(5000);
    QString status = statusProc.readAllStandardOutput().trimmed();

    if (status == "active") {
        emit logLine(tr("ZeroTier service is running"), MessageLevel::Launcher);
        m_step = 2;
        joinNetwork();
        return;
    }

    emit logLine(tr("Starting ZeroTier service..."), MessageLevel::Launcher);
    QProcess startProc;
    startProc.start("sudo", { "systemctl", "start", "zerotier-one" });
    startProc.waitForFinished(10000);

    if (startProc.exitCode() == 0) {
        emit logLine(tr("ZeroTier service started"), MessageLevel::Launcher);
    }
    m_step = 2;
    joinNetwork();
#else
    m_step = 2;
    joinNetwork();
#endif
}

void ZeroTierJoin::joinNetwork()
{
    emit logLine(tr("Joining ZeroTier network: %1").arg(m_networkId), MessageLevel::Launcher);
    m_process.start(m_ztPath, { "join", m_networkId });
}

void ZeroTierJoin::getLocalNodeId()
{
    emit logLine(tr("Getting local ZeroTier node ID..."), MessageLevel::Launcher);

    QProcess infoProc;
    infoProc.start(m_ztPath, { "info" });
    infoProc.waitForFinished(5000);
    QString info = infoProc.readAllStandardOutput().trimmed();

    // Parse: "200 info <nodeid> ..."
    QStringList parts = info.split(' ');
    if (parts.size() >= 3) {
        QString nodeId = parts[2];
        emit logLine(tr("Local node ID: %1").arg(nodeId), MessageLevel::Launcher);
        authorizeNode(nodeId);
    } else {
        emit logLine(tr("Could not get local node ID"), MessageLevel::Warning);
        emitSucceeded();
    }
}

void ZeroTierJoin::authorizeNode(const QString& nodeId)
{
    if (m_apiToken.isEmpty()) {
        emit logLine(tr("No API token set. Node must be manually authorized in ZeroTier Central."), MessageLevel::Warning);
        emit logLine(tr("ZeroTier IP assigned. Other players can connect."), MessageLevel::Launcher);
        emitSucceeded();
        return;
    }

    emit logLine(tr("Auto-authorizing node %1 on network %2...").arg(nodeId, m_networkId), MessageLevel::Launcher);

    // First check if already authorized
    QNetworkRequest checkReq(QUrl(QString("%1/networks/%2/members/%3").arg(ZT_API_BASE, m_networkId, nodeId)));
    checkReq.setRawHeader("Authorization", QString("bearer %1").arg(m_apiToken).toUtf8());
    checkReq.setRawHeader("Content-Type", "application/json");

    QNetworkReply* checkReply = m_nam.get(checkReq);
    connect(checkReply, &QNetworkReply::finished, this, [this, nodeId, checkReply]() {
        checkReply->deleteLater();

        if (checkReply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(checkReply->readAll());
            QJsonObject obj = doc.object();
            bool authorized = obj["authorized"].toBool(false);

            if (authorized) {
                emit logLine(tr("Node already authorized on network"), MessageLevel::Launcher);
                emit logLine(tr("ZeroTier IP assigned. Other players can connect."), MessageLevel::Launcher);
                emitSucceeded();
                return;
            }

            // Not authorized - authorize it
            QJsonObject authObj;
            authObj["authorized"] = true;

            QNetworkRequest authReq(QUrl(QString("%1/networks/%2/members/%3").arg(ZT_API_BASE, m_networkId, nodeId)));
            authReq.setRawHeader("Authorization", QString("bearer %1").arg(m_apiToken).toUtf8());
            authReq.setRawHeader("Content-Type", "application/json");

            QNetworkReply* authReply = m_nam.sendCustomRequest(authReq, "POST", QJsonDocument(authObj).toJson());
            connect(authReply, &QNetworkReply::finished, this, [this, authReply]() {
                authReply->deleteLater();
                if (authReply->error() == QNetworkReply::NoError) {
                    emit logLine(tr("Auto-authorization successful! Node approved."), MessageLevel::Launcher);
                } else {
                    emit logLine(tr("Auto-authorization failed: %1").arg(authReply->errorString()), MessageLevel::Warning);
                }

                // Show IP info
                QProcess ipProc;
                ipProc.start(m_ztPath, { "listnetworks" });
                ipProc.waitForFinished(5000);
                QString netInfo = ipProc.readAllStandardOutput();
                for (const auto& line : netInfo.split('\n')) {
                    if (line.contains(m_networkId)) {
                        emit logLine(tr("ZeroTier: %1").arg(line.trimmed()), MessageLevel::Launcher);
                    }
                }
                emit logLine(tr("Other players can connect using your ZeroTier IP"), MessageLevel::Launcher);
                emitSucceeded();
            });
        } else {
            emit logLine(tr("API check failed: %1").arg(checkReply->errorString()), MessageLevel::Warning);
            emitSucceeded();
        }
    });
}

void ZeroTierJoin::on_state(LoggedProcess::State state)
{
    switch (state) {
        case LoggedProcess::Aborted:
        case LoggedProcess::Crashed:
        case LoggedProcess::FailedToStart: {
            emit logLine(tr("ZeroTier join failed. Continuing without ZeroTier."), MessageLevel::Warning);
            emitSucceeded();
            return;
        }
        case LoggedProcess::Finished: {
            if (m_process.exitCode() == 0) {
                emit logLine(tr("Successfully joined ZeroTier network %1").arg(m_networkId), MessageLevel::Launcher);
                getLocalNodeId();
            } else {
                emit logLine(tr("ZeroTier join returned code %1. Continuing...").arg(m_process.exitCode()), MessageLevel::Warning);
                emitSucceeded();
            }
            return;
        }
        default:
            break;
    }
}

bool ZeroTierJoin::abort()
{
    auto state = m_process.state();
    if (state == LoggedProcess::Running || state == LoggedProcess::Starting) {
        m_process.kill();
    }
    return true;
}
