#ifndef ROBOT_H
#define ROBOT_H
#include <QObject>
#include "Wakeup/wakeup.h"
#include "Conversation/conversation.h"
#include "Recorder/player.h"
#ifdef SERVER
#include "Server/server.h"
#endif
#ifdef MQTT
#include "mqtt.h"
#endif
class Robot : public QObject
{
public:
    Robot(QObject* parent=nullptr);
    ~Robot();
    void start();
    void stop();
private:
    Wakeup* wakeup;
    Conversation* conversation;
    Player* player;
#ifdef SERVER
    Server* server;
#endif
#ifdef MQTT
    MQTTHandler* mqtt;
#endif
};

#endif // ROBOT_H
