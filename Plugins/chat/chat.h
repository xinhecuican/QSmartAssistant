#ifndef PLUGIN_CHAT_H
#define PLUGIN_CHAT_H
#include "../Plugin.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QObject>

class Chat : public QObject, Plugin {
    Q_OBJECT
    Q_INTERFACES(Plugin)
    Q_PLUGIN_METADATA(IID QSmartAssistant_PLUGIN_ID)
public:
    Chat();
    QString getName() override;
    bool handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                bool &isImmersive) override;
    void setPluginHelper(IPluginHelper *helper) override;
    void recvMessage(const QString &text, const ParsedIntent &parsedIntent,
                     const PluginMessage &message) override;
signals:
    void sendMessage(PluginMessage message) override;

private:
    void handleInner(const QString &text, const ParsedIntent &parsedIntent, int id,
                     const QString &master);
    void handleResponse(const QString &response, int id, const QString &master);

private:
    IPluginHelper *helper;
    QString botName;
    QString type;

    // chatgpt
    QNetworkAccessManager manager;
    QNetworkRequest request;
    QJsonObject requestData;
    QJsonArray conversation;
    QNetworkProxy proxy;
    QNetworkReply *reply;
    QString output;
    int currentToken;
    int maxTokens;
    int limitToken;
    QString result;
    bool stream;
    bool chatMode;
};

#endif
