#include "WebLLM.h"

WebLLM::WebLLM(const QJsonObject& config, QNetworkProxy& proxy, QObject *parent) : LLM(parent) {
    requestData["model"] = config.value("modelName").toString();
    key = config.value("key").toString();
    requestData["max_tokens"] = config.value("maxTokens").toInt(16000);
    request.setRawHeader("Authorization", ("Bearer " + key).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                        "application/json");
    request.setUrl(QUrl(config.value("api_base")
                     .toString("https://api.openai.com/v1/chat/completions")));
    reply = nullptr;
    this->proxy = proxy;
}

void WebLLM::query(LLMConversation* conversation) {
    requestData["messages"] = conversation->conversation;
    requestData["temperature"] = conversation->temperature;
    requestData["stream"] = conversation->stream;
    if (conversation->withProxy) {
        manager.setProxy(proxy);
    } else {
        manager.setProxy(QNetworkProxy::NoProxy);
    }

    if (reply != nullptr) {
        reply->deleteLater();
    }
    reply = manager.post(request, QJsonDocument(requestData).toJson());
    connect(reply, &QNetworkReply::readyRead, this, [=]() {
        QString response = QString::fromUtf8(reply->readAll());
        conversation->onResponse(response, false);
    });

    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() == QNetworkReply::NoError) {
            conversation->onResponse("", true);
        } else {
            qWarning() << "chatgpt network error" << reply->errorString();
        }
        eventLoop.quit();
    });
    eventLoop.exec(QEventLoop::ExcludeUserInputEvents);
}

void WebLLM::abort(int id) {
    if (reply != nullptr) {
        reply->abort();
    }
}