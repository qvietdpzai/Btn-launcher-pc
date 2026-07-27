#include "ZeroTierLeave.h"
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

ZeroTierLeave::ZeroTierLeave(LaunchTask* parent, const QString& networkId) : LaunchStep(parent), m_networkId(networkId)
{
    connect(&m_process, &LoggedProcess::log, this, &ZeroTierLeave::logLines);
    connect(&m_process, &LoggedProcess::stateChanged, this, &ZeroTierLeave::on_state);
    m_ztDir = FS::PathCombine(APPLICATION->dataRoot(), "zerotier");
}

QString ZeroTierLeave::findZerotierCli()
{
    QString bundled = FS::PathCombine(m_ztDir, ZT_CLI_NAME);
    if (QFileInfo::exists(bundled))
        return bundled;

    QString sysPath = QStandardPaths::findExecutable("zerotier-cli");
    if (!sysPath.isEmpty())
        return sysPath;

#ifdef Q_OS_WIN
    QStringList candidates = {
        "C:\\Program Files\\ZeroTier\\ZeroTier One\\zerotier-cli.exe",
        "C:\\Program Files (x86)\\ZeroTier\\ZeroTier One\\zerotier-cli.exe",
    };
#else
    QStringList candidates = {"/usr/sbin/zerotier-cli", "/usr/local/bin/zerotier-cli", "/snap/bin/zerotier-cli"};
#endif
    for (const auto& c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }
    return {};
}

void ZeroTierLeave::executeTask()
{
    QString ztPath = findZerotierCli();
    if (ztPath.isEmpty()) {
        emit logLine(tr("ZeroTier CLI not found, skipping leave."), MessageLevel::Warning);
        emitSucceeded();
        return;
    }

    emit logLine(tr("Leaving ZeroTier network: %1").arg(m_networkId), MessageLevel::Launcher);
    m_process.start(ztPath, { "leave", m_networkId });
}

void ZeroTierLeave::on_state(LoggedProcess::State state)
{
    switch (state) {
        case LoggedProcess::Aborted:
        case LoggedProcess::Crashed:
        case LoggedProcess::FailedToStart: {
            emit logLine(tr("ZeroTier leave failed."), MessageLevel::Warning);
            emitSucceeded();
            return;
        }
        case LoggedProcess::Finished: {
            if (m_process.exitCode() == 0) {
                emit logLine(tr("Successfully left ZeroTier network %1").arg(m_networkId), MessageLevel::Launcher);
            } else {
                emit logLine(tr("ZeroTier leave returned code %1.").arg(m_process.exitCode()), MessageLevel::Warning);
            }
            emitSucceeded();
            return;
        }
        default:
            break;
    }
}
