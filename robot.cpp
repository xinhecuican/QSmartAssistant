#include "robot.h"
#include "Utils/config.h"


Robot::Robot(QObject* parent) : QObject(parent)
{
    Config::instance()->loadConfig();
    wakeup = new Wakeup(this);
    conversation = new Conversation(this);
    connect(wakeup, &Wakeup::dataArrive, this, [=](QByteArray data){
        conversation->receiveData(data);
    });
    connect(wakeup, &Wakeup::detected, this, [=](bool stop){
        conversation->dialog(stop);
    });
    connect(conversation, &Conversation::finish, this, [=](){
        wakeup->resume();
    });
}

void Robot::start(){
    wakeup->startWakeup();
}

void Robot::stop(){
    wakeup->stopWakeup();
    conversation->stop();
}
