#ifndef __SERVER_H__
#define __SERVER_H__
#include <QtHttpServer/QHttpServer>
#include "../Conversation/conversation.h"

class Server : public QObject {
    Q_OBJECT
public:
    Server(Conversation* conversation, QObject* parent=nullptr);
signals:
    void requestASR(QString text, int id);
private:
    void increaseId();
private:
    Conversation* conversation;
    QHttpServer server;
    int port;
    uint32_t id;
    QMap<uint32_t, QEventLoop> waiting;
    QMap<uint32_t, QString> asrResult;
};

#endif