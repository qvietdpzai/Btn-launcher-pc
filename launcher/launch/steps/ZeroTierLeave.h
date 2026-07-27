#pragma once

#include "LoggedProcess.h"
#include "launch/LaunchStep.h"

class ZeroTierLeave : public LaunchStep {
    Q_OBJECT
   public:
    explicit ZeroTierLeave(LaunchTask* parent, const QString& networkId);
    virtual ~ZeroTierLeave() {};

    virtual void executeTask();
    virtual void finalize() {};

   private slots:
    void on_state(LoggedProcess::State state);

   private:
    QString findZerotierCli();
    LoggedProcess m_process;
    QString m_networkId;
    QString m_ztDir;
};
