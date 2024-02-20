#ifndef PLUGIN_H
#define PLUGIN_H
#include "../Utils/ParsedIntent.h"
#include <QObject>

class Conversation;

class Plugin : public QObject{
    Q_OBJECT
public:
    Plugin(QObject* parent=nullptr);
    virtual ~Plugin();

    virtual QString getName();

    virtual bool handle(const QString& text,
                        const ParsedIntent& parsedIntent,
                        bool& isImmersive)=0;
    void setConversation(Conversation* conversation);
protected:
    Conversation* conversation;
};

#endif // PLUGIN_H
