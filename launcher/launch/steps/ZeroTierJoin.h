#pragma once

#include "LoggedProcess.h"
#include "launch/LaunchStep.h"

#include <QNetworkAccessManager>

class ZeroTierJoin : public LaunchStep {
    Q_OBJECT
   public:
    explicit ZeroTierJoin(LaunchTask* parent, const QString& networkId, const QString& authToken = QString());
    virtual ~ZeroTierJoin() {};

    virtual void executeTask();
    virtual bool abort();
    virtual bool canAbort() const { return true; }

   private slots:
    void on_state(LoggedProcess::State state);
    void on_download_finished();

   private:
    QString findZerotierCli();
    void startService();
    void joinNetwork();
    void ensureServiceRunning();

    LoggedProcess m_process;
    QString m_networkId;
    QString m_authToken;
    QString m_ztPath;
    QString m_ztDir;
    QNetworkAccessManager m_nam;
    int m_step = 0; // 0=download, 1=start_service, 2=join
};
