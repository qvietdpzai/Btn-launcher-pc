#include "ZeroTierJoin.h"
#include <QStandardPaths>
#include <QFileInfo>
#include <launch/LaunchTask.h>

ZeroTierJoin::ZeroTierJoin(LaunchTask* parent, const QString& networkId) : LaunchStep(parent), m_networkId(networkId)
{
    connect(&m_process, &LoggedProcess::log, this, &ZeroTierJoin::logLines);
    connect(&m_process, &LoggedProcess::stateChanged, this, &ZeroTierJoin::on_state);
}

void ZeroTierJoin::executeTask()
{
    // First check if zerotier-cli exists
    QString ztPath = QStandardPaths::findExecutable("zerotier-cli");
    if (ztPath.isEmpty()) {
        // Try common install locations
        QStringList candidates = {
#ifdef Q_OS_WIN
            "C:\\Program Files\\ZeroTier\\ZeroTier One\\zerotier-cli.exe",
            "C:\\Program Files (x86)\\ZeroTier\\ZeroTier One\\zerotier-cli.exe",
#else
            "/usr/sbin/zerotier-cli",
            "/usr/local/bin/zerotier-cli",
            "/snap/bin/zerotier-cli",
#endif
        };
        for (const auto& c : candidates) {
            if (QFileInfo::exists(c)) {
                ztPath = c;
                break;
            }
        }
    }

    if (ztPath.isEmpty()) {
        emit logLine(tr("ZeroTier CLI not found. Please install ZeroTier from https://www.zerotier.com/download/"), MessageLevel::Warning);
        emit logLine(tr("Continuing launch without ZeroTier..."), MessageLevel::Warning);
        emitSucceeded();
        return;
    }

    emit logLine(tr("Joining ZeroTier network: %1").arg(m_networkId), MessageLevel::Launcher);

    // First check if already joined
    QProcess checkProcess;
    checkProcess.start(ztPath, { "listnetworks" });
    checkProcess.waitForFinished(5000);
    QString output = checkProcess.readAllStandardOutput();
    if (output.contains(m_networkId)) {
        emit logLine(tr("Already connected to ZeroTier network %1").arg(m_networkId), MessageLevel::Launcher);
        emitSucceeded();
        return;
    }

    // Join the network
    m_process.start(ztPath, { "join", m_networkId });
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
                emit logLine(tr("Other players can connect using your ZeroTier IP"), MessageLevel::Launcher);
            } else {
                emit logLine(tr("ZeroTier join returned code %1. Continuing without ZeroTier.").arg(m_process.exitCode()), MessageLevel::Warning);
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
