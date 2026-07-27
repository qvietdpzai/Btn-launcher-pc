#pragma once

#include "LoggedProcess.h"
#include "launch/LaunchStep.h"

#include <QNetworkAccessManager>

class ZeroTierJoin : public LaunchStep {
    Q_OBJECT
   public:
    explicit ZeroTierJoin(LaunchTask* parent, const QString& networkId, const QString& apiToken = QString());
    virtual ~ZeroTierJoin() {};

    virtual void executeTask();
    virtual bool abort();
    virtual bool canAbort() const { return true; }

   private slots:
    void on_state(LoggedProcess::State state);
    void on_api_reply(QNetworkReply* reply);

   private:
    QString findZerotierCli();
    void ensureServiceRunning();
    void joinNetwork();
    void getLocalNodeId();
    void authorizeNode(const QString& nodeId);

    LoggedProcess m_process;
    QString m_networkId;
    QString m_apiToken;
    QString m_ztPath;
    QString m_ztDir;
    QNetworkAccessManager m_nam;
    int m_step = 0;
};
