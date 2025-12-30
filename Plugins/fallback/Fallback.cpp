#include "Fallback.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

Fallback::Fallback() {}

QString Fallback::getName() { return "Fallback"; }

void Fallback::setPluginHelper(IPluginHelper *helper) { 
    this->helper = helper; 
    this->llmManager = helper->getLLMManager();
    this->conversation = new LLMConversation();
    QJsonObject payload;
    payload.insert("role", "system");
    payload.insert("content", "你是一名语言专家，用户的输入为语音识别后的句子。它可能是有意义的命令但是存在一些识别错误，也可能 \
                    是误触接收到的毫无意义的句子，也可能是用户提出的问题。你需要判断该句是否有意义，或者是否为一个问题，\
                    如果是一个问题输出2,如果该句有意义则输出1，如果是无意义的句子则输出0。");
    conversation->conversation.append(payload);

    conversation->stream = false;
    conversation->temperature = 0;
    conversation->withProxy = false;
    conversation->onResponse = [=](const QString& text, bool last){
        QString content;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(text.toUtf8());
        QJsonObject jsonObj = jsonDoc.object();
        if (jsonObj.contains("choices")) {
            QJsonArray choicesArray = jsonObj["choices"].toArray();
            if (!choicesArray.isEmpty()) {
                QJsonObject messageObj =
                    choicesArray.at(0).toObject()["message"].toObject();
                content = messageObj["content"].toString();
            }
        }
        if (content == "2") {
            PluginMessage message;
            message.id = this->currentId;
            message.dst = "Chat";
            message.message = "chat";
            emit sendMessage(message);
        } else if (content == "1") {
            helper->say("您说的是" + this->text, this->currentId);
        }
    };
}

Fallback::~Fallback() {
    delete conversation;
}

bool Fallback::handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                      bool &isImmersive) {
    if (conversation->conversation.size() > 1) {
        conversation->conversation.removeAt(1);
    }
    conversation->conversation.append(QJsonObject{{"role", "user"}, {"content", text}});
    this->text = text;
    this->currentId = id;
    llmManager->query("", conversation);
    return true;
}

void Fallback::recvMessage(const QString &text,
                           const ParsedIntent &parsedIntent,
                           const PluginMessage &message) {}