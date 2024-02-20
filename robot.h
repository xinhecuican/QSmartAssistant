#ifndef ROBOT_H
#define ROBOT_H
#include <QObject>
#include "Wakeup/wakeup.h"
#include "Conversation/conversation.h"

class Robot : public QObject
{
public:
    Robot(QObject* parent=nullptr);
    void start();
    void stop();
private:
    Wakeup* wakeup;
    Conversation* conversation;
};

#endif // ROBOT_H
