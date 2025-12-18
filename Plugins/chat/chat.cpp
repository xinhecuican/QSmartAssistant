#include "chat.h"
#include "../../Utils/config.h"
#include <QDebug>

Chat::Chat() : chatMode(false), conversation(nullptr) {
}

Chat::~Chat() {
    if (conversation != nullptr) {
        delete conversation;
    }
}

QString Chat::getName() { return "Chat"; }

void Chat::setPluginHelper(IPluginHelper *helper) {
    this->helper = helper;
    QJsonObject chatConfig = helper->getConfig()->getConfig("chat");
    llmManager = helper->getLLMManager();
    conversation = new LLMConversation();
    modelName = chatConfig.value("modelName").toString();
    double temperature = chatConfig.value("temperature").toDouble(1);
    limitToken = chatConfig.value("limit_token").toInt(4000);
    stream = chatConfig.value("stream").toBool();
    QString payloadStr = chatConfig.value("payload").toString();

    if (payloadStr != "") {
        payload.insert("role", "system");
        payload.insert("content", payloadStr);
    } else {
        payload.insert("role", "system");
        payload.insert("content", "你是一个非常实用的AI助手");
    }
    conversation->temperature = temperature;
    conversation->stream = stream;
    conversation->conversation.append(payload);
    currentToken += payload["content"].toString().size();

    conversation->onResponse = [=](QString text, bool last){
        if (last) {
            if (output != "") {
                helper->say(output, currentId, false, master);
                output.clear();
            }
            QJsonObject response;
            response.insert("role", "assistant");
            response.insert("content", result);
            conversation->conversation.append(response);
            currentToken += result.size();
            while (currentToken > limitToken &&
                    conversation->conversation.size() > 1) {
                currentToken -=
                    conversation->conversation.at(1)["content"].toString().size();
                conversation->conversation.removeAt(1);
            }
        } else {
            QStringList list = text.split("\n\n");
            if (list.last() == "") {
                list.removeLast();
            }
            for (QString &response : list) {
                for (int i = 0; i < response.size(); i++) {
                    if (response[i] == '{') {
                        response.remove(0, i);
                        break;
                    }
                }
                handleResponse(response, currentId, master);
            }
        }
    };
}

bool Chat::handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                  bool &isImmersive) {
    if (isImmersive) {
        llmManager->abort(modelName, id);
        result = "";
        helper->stopSay("chat");
        if (!chatMode)
            isImmersive = false;
    }
    if (parsedIntent.hasIntent("CLOSE") && chatMode) {
        isImmersive = false;
        conversation->conversation = QJsonArray();
        conversation->conversation.append(payload);
        return true;
    } else if (parsedIntent.hasIntent("CHAT") || chatMode) {
        isImmersive = true;
        if (!chatMode) {
            currentId = id;
            QString ans = helper->question("来对话吧！您有什么问题呢？");
            chatMode = true;
            if (ans != "") {
                handleInner(ans, parsedIntent, id, "Chat");
            }
        } else {
            handleInner(text, parsedIntent, id, "Chat");
        }
        return true;
    }
    return false;
}

void Chat::recvMessage(const QString &text, const ParsedIntent &intent,
                       const PluginMessage &message) {
    if (message.message == "chat") {
        handleInner(text, intent, message.id, message.src);
    } else if (message.message == "quit") {
        llmManager->abort(modelName, currentId);
        result = "";
        helper->stopSay("chat");
    }
}

void Chat::handleResponse(const QString &response, int id, const QString &master) {
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
                        content[i] == QChar(0x3002) ||
                        content[i] == QChar(0xff01) ||
                        content[i] == '!' || content[i] == '?' ||
                        content[i] == QChar(0xff1f) ||
                        content[i] == QChar(0xff1b) ||
                        content[i] == '\n') {
                        split = true;
                        list.append(content.mid(begin, i - begin));
                        begin = i;
                    }
                }
                if (split) {
                    if (list.size() > 1) {
                        helper->say(output + list.at(0), id, false, master);
                        for (int i = 1; i < list.size() - 1; i++) {
                            helper->say(list.at(i), id, false, master);
                        }
                        output = list.last();
                    } else {
                        helper->say(output + list.at(0), id, false, master);
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
}

void Chat::handleInner(const QString &text, const ParsedIntent &intent, int id,
                       const QString &master) {
    this->master = master;
    conversation->conversation.append(QJsonObject{{"role", "user"}, {"content", text}});
    currentToken += text.size();
    llmManager->query(modelName, conversation);
}
