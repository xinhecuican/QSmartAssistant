#include "LLMManager.h"
#include <QJsonObject>
#include "../Utils/config.h"
#include "WebLLM.h"

LLMManager::LLMManager(QObject *parent) : QObject(parent) {

    QJsonObject config = Config::instance()->getConfig("llm");
    QString proxy = config.value("proxy").toString();
    if (proxy != "") {
        QStringList url = proxy.split(':');
        if (url.size() == 2) {
            this->proxy.setType(QNetworkProxy::HttpProxy);
            this->proxy.setHostName(url.at(0));
            this->proxy.setPort(url.at(1).toShort());
        }
    }

    for (auto it = config.begin(); it != config.end(); ++it) {
        QString name = it.key();
        QJsonObject llmConfig = it.value().toObject();
        WebLLM* llm = new WebLLM(llmConfig, this->proxy, this);
        llms[name] = llm;
    }
}

bool LLMManager::query(const QString& name, LLMConversation* conversation) {
    if (name == "") {
        llms.begin().value()->query(conversation);
        return true;
    }
    else if (llms.contains(name)) {
        llms[name]->query(conversation);
        return true;
    }
    return false;
}

void LLMManager::abort(const QString& name, int id) {
    if (name == "") {
        llms.begin().value()->abort(id);
    }
    else if (llms.contains(name)) {
        llms[name]->abort(id);
    }
}
