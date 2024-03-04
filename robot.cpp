#include "robot.h"
#include "Utils/config.h"
#include <QCoreApplication>

Robot::Robot(QObject* parent) : QObject(parent)
{
    player = new Player(this);
    Config::instance()->loadConfig();
    wakeup = new Wakeup(player, this);
    conversation = new Conversation(player, this);
    connect(wakeup, &Wakeup::dataArrive, this, [=](QByteArray data){
        conversation->receiveData(data);
    });
    connect(wakeup, &Wakeup::detected, this, [=](bool stop){
        conversation->dialog(stop);
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
