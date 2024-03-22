#include "chat.h"
#include "../../Utils/config.h"

Chat::Chat() : reply(nullptr) {}

QString Chat::getName(){ return "Chat"; }

void Chat::setPluginHelper(IPluginHelper *helper) {
    this->helper = helper;
    QJsonObject chatConfig = helper->getConfig()->getConfig("chat");
    botName = chatConfig.value("botName").toString();
    if (botName == "chatgpt") {
        QString modelName = chatConfig.value("modelName").toString();
        QString key = chatConfig.value("key").toString();
        double temperature = chatConfig.value("temperature").toDouble(1);
        maxTokens = chatConfig.value("max_tokens").toInt(1000);
        bool stream = chatConfig.value("stream").toBool();
        QString payload = chatConfig.value("payload").toString();
        if (payload != "") {
            this->payload.insert("role", "system");
            this->payload.insert("content", payload);
        } else {
            this->payload.insert("role", "system");
            this->payload.insert("content", "你是一个非常实用的AI助手");
        }
        requestData["model"] = modelName;
        requestData["temperature"] = temperature;
        requestData["max_tokens"] = maxTokens;
        requestData["stream"] = stream;
        request.setRawHeader("Authorization", ("Bearer " + key).toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          "application/json");
        request.setUrl(QUrl("https://api.openai.com/v1/chat/completions"));

        QString proxy = chatConfig.value("proxy").toString();
        if (proxy != "") {
            QStringList url = proxy.split(':');
            if (url.size() == 2) {
                this->proxy.setType(QNetworkProxy::HttpProxy);
                this->proxy.setHostName(url.at(0));
                this->proxy.setPort(url.at(1).toShort());
                qDebug() << this->proxy.hostName() << this->proxy.port();
                manager.setProxy(this->proxy);
            }
        }
    } else {
        qWarning() << "unknown chat name";
    }
}

bool Chat::handle(const QString &text, const ParsedIntent &parsedIntent,
                  bool &isImmersive) {
    if (parsedIntent.hasIntent("CHAT")) {
        helper->say("来对话吧！您有什么问题呢？", true);
        while (true) {
            QString ans = helper->question("");
            if (ans == "")
                continue;
            ParsedIntent intents = helper->parse(ans);
            if (isImmersive) {
                reply->abort();
                result = "";
                helper->stopSay("chat");
                isImmersive = false;
            }
            if (intents.hasIntent("CLOSE_MUSIC")) {
                break;
            } else {
                isImmersive = true;
                handleInner(ans, intents, "Chat");
            }
        }
        isImmersive = false;
        return true;
    }
    return false;
}

void Chat::recvMessage(const QString &text, const ParsedIntent &intent,
                       const PluginMessage &message) {
    if (message.message == "chat") {
        handleInner(text, intent, message.src);
    } else if (message.message == "quit") {
        reply->abort();
        result = "";
        helper->stopSay("chat");
    }
}

void Chat::handleInner(const QString &text, const ParsedIntent &intent,
                       const QString &master) {
    if (botName == "chatgpt") {
        requestData["messages"] =
            QJsonArray{payload, conversation,
                       QJsonObject{{"role", "user"}, {"content", text}}};
        if(reply != nullptr){
            delete reply;
        }
        reply = manager.post(request, QJsonDocument(requestData).toJson());
        connect(reply, &QNetworkReply::readyRead, this, [=]() {
            QString response = QString::fromUtf8(reply->readAll());
            QJsonDocument jsonDoc = QJsonDocument::fromJson(response.toUtf8());
            QJsonObject jsonObj = jsonDoc.object();
            QJsonArray choicesArray = jsonObj["choices"].toArray();
            if (!choicesArray.isEmpty()) {
                QJsonObject messageObj =
                    choicesArray[0].toObject()["message"].toObject();
                response = messageObj["content"].toString();
                result += response;
                QStringList list = response.split(QRegExp("\t|.|。|!|\?|；|\n"),
                                                  Qt::SkipEmptyParts);
                if (list.size() > 1) {
                    helper->say(output + list.at(0), false, master);
                    for (int i = 1; i < list.size() - 1; i++) {
                        helper->say(list.at(i), false, master);
                    }
                    output = list.last();
                } else {
                    output = response;
                }
            }
        });
        QObject::connect(reply, &QNetworkReply::finished, this, [=]() {
            if (reply->error() == QNetworkReply::NoError) {
                if (output != "") {
                    helper->say(output, false, master);
                    output.clear();
                }
                QJsonObject question;
                question.insert("role", "user");
                question.insert("content", text);
                currentToken += text.size();
                conversation.append(question);
                QJsonObject response;
                response.insert("role", "assistant");
                response.insert("content", result);
                conversation.append(result);
                currentToken += result.size();
                while (currentToken + 500 > maxTokens) {
                    currentToken -=
                        conversation.first()["content"].toString().size();
                    conversation.removeFirst();
                }
            } else {
                qWarning() << "chatgpt network error" << reply->errorString();
            }
            result.clear();
            helper->quitImmersive(master);
        });
    }
}
