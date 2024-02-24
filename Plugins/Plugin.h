#ifndef PLUGIN_H
#define PLUGIN_H
#include "../Utils/ParsedIntent.h"
#include <QObject>
#include "IPluginHelper.h"

class Conversation;

class Plugin : public QObject{
    Q_OBJECT
public:
    Plugin(IPluginHelper* helper, QObject* parent=nullptr);
    virtual ~Plugin();

    virtual QString getName();

    virtual bool handle(const QString& text,
                        const ParsedIntent& parsedIntent,
                        bool& isImmersive)=0;
protected:
    IPluginHelper* helper;
};

#endif // PLUGIN_H
