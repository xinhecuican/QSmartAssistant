#include "chat.h"
#include "../../Utils/config.h"

Chat::Chat() : reply(nullptr), chatMode(false) {}

QString Chat::getName() { return "Chat"; }

void Chat::setPluginHelper(IPluginHelper *helper) {
    this->helper = helper;
    QJsonObject chatConfig = helper->getConfig()->getConfig("chat");
    botName = chatConfig.value("botName").toString();
    if (botName == "chatgpt") {
        QString modelName = chatConfig.value("modelName").toString();
        QString key = chatConfig.value("key").toString();
        double temperature = chatConfig.value("temperature").toDouble(1);
        maxTokens = chatConfig.value("max_tokens").toInt(1000);
        limitToken = chatConfig.value("limit_token").toInt(4000);
        stream = chatConfig.value("stream").toBool();
        QString payloadStr = chatConfig.value("payload").toString();
        QJsonObject payload;
        if (payloadStr != "") {
            payload.insert("role", "system");
            payload.insert("content", payloadStr);
        } else {
            payload.insert("role", "system");
            payload.insert("content", "你是一个非常实用的AI助手");
        }
        conversation.append(payload);
        currentToken += payload["content"].toString().size();
        requestData["model"] = modelName;
        requestData["temperature"] = temperature;
        requestData["max_tokens"] = maxTokens;
        requestData["stream"] = stream;
        request.setRawHeader("Authorization", ("Bearer " + key).toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          "application/json");
        request.setUrl(
            QUrl(chatConfig.value("api_base")
                     .toString("https://api.openai.com/v1/chat/completions")));

        QString proxy = chatConfig.value("proxy").toString();
        if (proxy != "") {
            QStringList url = proxy.split(':');
            if (url.size() == 2) {
                this->proxy.setType(QNetworkProxy::HttpProxy);
                this->proxy.setHostName(url.at(0));
                this->proxy.setPort(url.at(1).toShort());
                manager.setProxy(this->proxy);
            }
        }
    } else {
        qWarning() << "unknown chat name";
    }
}

bool Chat::handle(const QString &text, const ParsedIntent &parsedIntent,
                  bool &isImmersive) {
    if (isImmersive) {
        reply->abort();
        result = "";
        helper->stopSay("chat");
        if (!chatMode)
            isImmersive = false;
    }
    if (parsedIntent.hasIntent("CLOSE_MUSIC") && chatMode) {
        isImmersive = false;
        return true;
    } else if (parsedIntent.hasIntent("CHAT") || chatMode) {
        isImmersive = true;
        if (!chatMode) {
            QString ans = helper->question("来对话吧！您有什么问题呢？");
            chatMode = true;
            if (ans != "") {
                handleInner(ans, parsedIntent, "Chat");
            }
        } else {
            handleInner(text, parsedIntent, "Chat");
        }
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
        conversation.append(QJsonObject{{"role", "user"}, {"content", text}});
        requestData["messages"] = conversation;
        currentToken += text.size();
        if (reply != nullptr) {
            delete reply;
        }
        qDebug() << requestData;
        reply = manager.post(request, QJsonDocument(requestData).toJson());
        connect(reply, &QNetworkReply::readyRead, this, [=]() {
            QString response = QString::fromUtf8(reply->readAll());
            for (int i = 0; i < response.size(); i++) {
                if (response[i] == '{') {
                    response.remove(0, i);
                    break;
                }
            }
            QJsonDocument jsonDoc = QJsonDocument::fromJson(response.toUtf8());
            QJsonObject jsonObj = jsonDoc.object();
            if (jsonObj.contains("choices")) {
                QJsonArray choicesArray = jsonObj["choices"].toArray();
                QString content;
                if (!choicesArray.isEmpty()) {
                    if (!stream) {
                        QJsonObject messageObj =
                            choicesArray.at(0).toObject()["message"].toObject();
                        content = messageObj["content"].toString();
                    } else {
                        QJsonObject message = choicesArray.at(0).toObject();
                        QJsonObject messageObj = message["delta"].toObject();
                        if (messageObj.contains("content")) {
                            content = messageObj["content"].toString();
                        }
                    }
                    result += content;
                    bool split = false;
                    QStringList list;
                    int begin = -1;
                    if (stream) {
                        for (int i = 0; i < content.size(); i++) {
                            if (content[i] == '\t' || content[i] == '.' ||
                                content[i] == "。" || content[i] == "！" ||
                                content[i] == '!' || content[i] == '?' ||
                                content[i] == "？" || content[i] == "；" ||
                                content[i] == '\n') {
                                split = true;
                                list.append(content.mid(begin, i - begin));
                                begin = i;
                            }
                        }
                        if (split) {
                            if (list.size() > 1) {
                                helper->say(output + list.at(0), false, master);
                                for (int i = 1; i < list.size() - 1; i++) {
                                    helper->say(list.at(i), false, master);
                                }
                                output = list.last();
                            } else {
                                helper->say(output + list.at(0), false, master);
                                output = "";
                            }
                        } else {
                            output += content;
                        }
                    } else {
                        output += content;
                    }
                }
            }
        });
        QObject::connect(reply, &QNetworkReply::finished, this, [=]() {
            if (reply->error() == QNetworkReply::NoError) {
                if (output != "") {
                    helper->say(output, false, master);
                    output.clear();
                }
                QJsonObject response;
                response.insert("role", "assistant");
                response.insert("content", result);
                conversation.append(response);
                currentToken += result.size();
                while (currentToken + maxTokens > limitToken &&
                       conversation.size() > 1) {
                    currentToken -=
                        conversation.at(1)["content"].toString().size();
                    conversation.removeAt(1);
                }
            } else {
                qWarning() << "chatgpt network error" << reply->errorString();
            }
            result.clear();
            if (!chatMode)
                helper->quitImmersive(master);
        });
    }
}
