#ifndef QUITROBOT_H
#define QUITROBOT_H
#include "Plugin.h"
class QuitRobot : public Plugin
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit QuitRobot(IPluginHelper* helper, QObject* parent=nullptr);
    QString getName() override;
    bool handle(const QString& text,
                const ParsedIntent& parsedIntent,
                bool& isImmersive) override;
};

#endif // QUITROBOT_H
