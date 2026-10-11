#include "robot.h"
#include "Utils/config.h"
#include <QCoreApplication>

Robot::Robot(QObject* parent) : QObject(parent)
{
    player = new Player(this);
    Config::instance()->loadConfig();
    wakeup = new Wakeup(player, this);
    conversation = new Conversation(player, this);
#ifdef SERVER
    server = new Server(this);
    // Server 只做传输，不认识 Conversation：这里把两端接起来。
    // 返回值是「同步回复」（立即开口的插件），异步回复由 responseText 推送。
    server->setQuestionHandler(
        [this](const QString &text, int id, bool canAnswer) {
            return conversation->intentRequest(text, id, canAnswer);
        });
    connect(conversation, &Conversation::responseText, server,
            &Server::pushResponse);
    // 反问（pushQuestion / answer）：插件处理到一半要用户再给一个输入时，
    // 问题推给网页，网页的作答再送回对话逻辑。语音那条通道在
    // Conversation::question() 里，两条路先到先得，Server 只负责搬运。
    connect(conversation, &Conversation::requestQuestion, server,
            &Server::pushQuestion);
    server->setAnswerHandler([this](int id, const QString &text) {
        conversation->onExternalAnswer(id, text);
    });
#endif
#ifdef MQTT
    mqtt = new MQTTHandler(this);
    connect(wakeup, &Wakeup::wakeup, mqtt, &MQTTHandler::onWakeup);
    connect(wakeup, &Wakeup::detected, mqtt, &MQTTHandler::onDetect);
    connect(conversation, &Conversation::asrRecognize, mqtt, &MQTTHandler::onASR);
    connect(conversation, &Conversation::sayText, mqtt, &MQTTHandler::onSay);
#endif
    connect(wakeup, &Wakeup::dataArrive, this, [=](QByteArray data){
        conversation->receiveData(data);
    });
    connect(wakeup, &Wakeup::detected, this, [=](bool stop){
        conversation->dialog(stop);
    });
    connect(wakeup, &Wakeup::detectedIntent, this, [=](const QString& text, const ParsedIntent& intent){
        conversation->handlePlugin(text, intent, 0);
    });
    connect(wakeup, &Wakeup::finishResponse, conversation, &Conversation::onResponse);
    connect(conversation, &Conversation::finish, this, [=](){
        wakeup->resume();
    });
    connect(conversation, &Conversation::requestResponse, wakeup, &Wakeup::doResponse);
    connect(conversation, &Conversation::exitSig, this, [=](){
        stop();
        QCoreApplication::exit();
    });
}

Robot::~Robot(){

}

void Robot::start(){
    wakeup->startWakeup();
}

void Robot::stop(){
    wakeup->stopWakeup();
    conversation->stop();
}
