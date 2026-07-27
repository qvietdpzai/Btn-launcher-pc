#pragma once

#include "LoggedProcess.h"
#include "launch/LaunchStep.h"

class ZeroTierJoin : public LaunchStep {
    Q_OBJECT
   public:
    explicit ZeroTierJoin(LaunchTask* parent, const QString& networkId);
    virtual ~ZeroTierJoin() {};

    virtual void executeTask();
    virtual bool abort();
    virtual bool canAbort() const { return true; }

   private slots:
    void on_state(LoggedProcess::State state);

   private:
    QString findZerotierCli();
    void ensureServiceRunning();
    void joinNetwork();

    LoggedProcess m_process;
    QString m_networkId;
    QString m_ztPath;
    QString m_ztDir;
    int m_step = 0;
};
