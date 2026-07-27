#include "ZeroTierJoin.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <launch/LaunchTask.h>
#include "Application.h"
#include "FileSystem.h"

#ifdef Q_OS_WIN
static const QString ZT_CLI_NAME = "zerotier-cli.exe";
#else
static const QString ZT_CLI_NAME = "zerotier-cli";
#endif

ZeroTierJoin::ZeroTierJoin(LaunchTask* parent, const QString& networkId)
    : LaunchStep(parent), m_networkId(networkId)
{
    connect(&m_process, &LoggedProcess::log, this, &ZeroTierJoin::logLines);
    connect(&m_process, &LoggedProcess::stateChanged, this, &ZeroTierJoin::on_state);
    m_ztDir = FS::PathCombine(APPLICATION->dataRoot(), "zerotier");
    FS::ensureFolderPathExists(m_ztDir);
}

QString ZeroTierJoin::findZerotierCli()
{
    QString bundled = FS::PathCombine(m_ztDir, ZT_CLI_NAME);
    if (QFileInfo::exists(bundled))
        return bundled;

    QString sysPath = QStandardPaths::findExecutable("zerotier-cli");
    if (!sysPath.isEmpty())
        return sysPath;

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
        emit logLine(tr("ZeroTier not found. Starting service..."), MessageLevel::Launcher);
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
            emit logLine(tr("ZeroTier service started"), MessageLevel::Launcher);
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

    emit logLine(tr("ZeroTier found: %1").arg(m_ztPath), MessageLevel::Launcher);

    // Check if already joined
    QProcess checkProcess;
    checkProcess.start(m_ztPath, { "listnetworks" });
    checkProcess.waitForFinished(5000);
    QString output = checkProcess.readAllStandardOutput();
    if (output.contains(m_networkId)) {
        emit logLine(tr("Already connected to network %1").arg(m_networkId), MessageLevel::Launcher);

        // Show IP
        for (const auto& line : output.split('\n')) {
            if (line.contains(m_networkId)) {
                emit logLine(line.trimmed(), MessageLevel::Launcher);
            }
        }
        emit logLine(tr("Other players can connect using your ZeroTier IP"), MessageLevel::Launcher);
        emitSucceeded();
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

                // Show network info and IP
                QProcess ipProc;
                ipProc.start(m_ztPath, { "listnetworks" });
                ipProc.waitForFinished(5000);
                QString netInfo = ipProc.readAllStandardOutput();
                for (const auto& line : netInfo.split('\n')) {
                    if (line.contains(m_networkId)) {
                        emit logLine(line.trimmed(), MessageLevel::Launcher);
                    }
                }
                emit logLine(tr("Other players can connect using your ZeroTier IP"), MessageLevel::Launcher);
            } else {
                emit logLine(tr("ZeroTier join failed (code %1). Continuing...").arg(m_process.exitCode()), MessageLevel::Warning);
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
