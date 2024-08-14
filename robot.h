#ifndef ROBOT_H
#define ROBOT_H
#include <QObject>
#include "Wakeup/wakeup.h"
#include "Conversation/conversation.h"
#include "Recorder/player.h"
#ifdef SERVER
#include "Server/server.h"
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
    Server* server;
};

#endif // ROBOT_H
