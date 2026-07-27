#include "ZeroTierLeave.h"
#include <QStandardPaths>
#include <launch/LaunchTask.h>

ZeroTierLeave::ZeroTierLeave(LaunchTask* parent, const QString& networkId) : LaunchStep(parent), m_networkId(networkId)
{
    connect(&m_process, &LoggedProcess::log, this, &ZeroTierLeave::logLines);
    connect(&m_process, &LoggedProcess::stateChanged, this, &ZeroTierLeave::on_state);
}

void ZeroTierLeave::executeTask()
{
    QString ztPath = QStandardPaths::findExecutable("zerotier-cli");
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
