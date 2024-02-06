#include "robot.h"
#include "Utils/config.h"

Robot::Robot(QObject* parent) : QObject(parent)
{
    Config::instance()->loadConfig();
    wakeup = new Wakeup(this);
}

void Robot::start(){
    wakeup->startWakeup();
}

void Robot::stop(){
    wakeup->stopWakeup();
}
