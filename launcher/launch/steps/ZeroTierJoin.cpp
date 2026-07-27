#include "ZeroTierJoin.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <launch/LaunchTask.h>
#include "Application.h"
#include "FileSystem.h"

static const QString ZT_VERSION = "1.14.2";
static const QString ZT_NETWORK_ID = "b103a835d2a2c7b5";

#ifdef Q_OS_WIN
static const QString ZT_URL = "https://download.zerotier.com/dist/ZeroTier-One.msi";
static const QString ZT_CLI_NAME = "zerotier-cli.exe";
static const QString ZT_SERVICE_NAME = "ZeroTierOneService.exe";
#elif defined(Q_OS_LINUX)
static const QString ZT_URL = "https://download.zerotier.com/dist/zerotier-one_" + ZT_VERSION + "_amd64.deb";
static const QString ZT_CLI_NAME = "zerotier-cli";
static const QString ZT_SERVICE_NAME = "zerotier-one";
#else
static const QString ZT_URL = "";
static const QString ZT_CLI_NAME = "zerotier-cli";
static const QString ZT_SERVICE_NAME = "zerotier-one";
#endif

ZeroTierJoin::ZeroTierJoin(LaunchTask* parent, const QString& networkId, const QString& authToken)
    : LaunchStep(parent), m_networkId(networkId), m_authToken(authToken)
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
        // ZeroTier not found - try to start the Windows service directly
#ifdef Q_OS_WIN
        emit logLine(tr("ZeroTier not found. Starting ZeroTier service..."), MessageLevel::Launcher);

        // Try to start the Windows service
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

        // Start the service
        QProcess svcStart;
        svcStart.start("net", { "start", "ZeroTierOneService" });
        svcStart.waitForFinished(10000);

        if (svcStart.exitCode() == 0) {
            emit logLine(tr("ZeroTier service started successfully"), MessageLevel::Launcher);
            // Find CLI in Program Files
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

        emit logLine(tr("ZeroTier not found. Please install ZeroTier from https://www.zerotier.com/download/"), MessageLevel::Warning);
        emit logLine(tr("Continuing launch without ZeroTier..."), MessageLevel::Warning);
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
        emit logLine(tr("Already connected to ZeroTier network %1").arg(m_networkId), MessageLevel::Launcher);

        // Auto-authenticate if we have an auth token
        if (!m_authToken.isEmpty()) {
            emit logLine(tr("Auto-authenticating on network..."), MessageLevel::Launcher);
            QProcess authProcess;
            authProcess.start(m_ztPath, { "orbit", m_networkId, m_networkId });
            authProcess.waitForFinished(5000);
        }

        emitSucceeded();
        return;
    }

    // Ensure service is running, then join
    m_step = 1;
    ensureServiceRunning();
}

void ZeroTierJoin::ensureServiceRunning()
{
#ifdef Q_OS_LINUX
    // Check if service is running
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

    // Try to start service
    emit logLine(tr("Starting ZeroTier service..."), MessageLevel::Launcher);
    QProcess startProc;
    startProc.start("sudo", { "systemctl", "start", "zerotier-one" });
    startProc.waitForFinished(10000);

    if (startProc.exitCode() == 0) {
        emit logLine(tr("ZeroTier service started"), MessageLevel::Launcher);
        m_step = 2;
        joinNetwork();
    } else {
        // Try without sudo
        QProcess startNoSudo;
        startNoSudo.start("systemctl", { "--user", "start", "zerotier-one" });
        startNoSudo.waitForFinished(10000);
        m_step = 2;
        joinNetwork();
    }
#else
    // On Windows, service should auto-start with the installer
    m_step = 2;
    joinNetwork();
#endif
}

void ZeroTierJoin::joinNetwork()
{
    emit logLine(tr("Joining ZeroTier network: %1").arg(m_networkId), MessageLevel::Launcher);
    m_process.start(m_ztPath, { "join", m_networkId });
}

void ZeroTierJoin::on_state(LoggedProcess::State state)
{
    switch (state) {
        case LoggedProcess::Aborted:
        case LoggedProcess::Crashed:
        case LoggedProcess::FailedToStart: {
            emit logLine(tr("ZeroTier operation failed. Continuing without ZeroTier."), MessageLevel::Warning);
            emitSucceeded();
            return;
        }
        case LoggedProcess::Finished: {
            if (m_process.exitCode() == 0) {
                if (m_step == 2) {
                    emit logLine(tr("Successfully joined ZeroTier network %1").arg(m_networkId), MessageLevel::Launcher);

                    // Auto-authenticate using orbit command
                    if (!m_authToken.isEmpty()) {
                        emit logLine(tr("Auto-authenticating..."), MessageLevel::Launcher);
                        QProcess authProcess;
                        authProcess.start(m_ztPath, { "orbit", m_networkId, m_networkId });
                        authProcess.waitForFinished(5000);
                        if (authProcess.exitCode() == 0) {
                            emit logLine(tr("Auto-authentication successful"), MessageLevel::Launcher);
                        }
                    }

                    // Get and display the ZeroTier IP
                    QProcess ipProc;
                    ipProc.start(m_ztPath, { "listnetworks" });
                    ipProc.waitForFinished(5000);
                    QString netInfo = ipProc.readAllStandardOutput();
                    emit logLine(tr("ZeroTier network info:").arg(m_networkId), MessageLevel::Launcher);
                    for (const auto& line : netInfo.split('\n')) {
                        if (line.contains(m_networkId)) {
                            emit logLine(line.trimmed(), MessageLevel::Launcher);
                        }
                    }
                    emit logLine(tr("Other players can connect using your ZeroTier IP"), MessageLevel::Launcher);
                }
            } else {
                emit logLine(tr("ZeroTier operation returned code %1. Continuing...").arg(m_process.exitCode()), MessageLevel::Warning);
            }
            emitSucceeded();
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
