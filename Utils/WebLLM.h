#ifndef WEB_LLM_H
#define WEB_LLM_H
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QEventLoop>
#include <QJsonObject>
#include "LLM.h"
#include "LPcommonGlobal.h"

class LPCOMMON_EXPORT WebLLM : public LLM {
public:
    WebLLM(const QJsonObject& config, QNetworkProxy& proxy, QObject *parent = nullptr);
    void query(LLMConversation* conversation) override;
    void abort(int id) override;

private:
    QNetworkAccessManager manager;
    QNetworkRequest request;
    QNetworkProxy proxy;
    QNetworkReply *reply;
    QString modelName;
    QString key;
    QJsonObject requestData;
    int currentToken;
    int maxTokens;
    int limitToken;
    QEventLoop eventLoop;
};
#endif