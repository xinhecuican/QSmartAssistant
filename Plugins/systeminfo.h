#ifndef SYSTEMINFO_H
#define SYSTEMINFO_H
#include "Plugin.h"

class SystemInfo : public Plugin
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit SystemInfo(IPluginHelper* helper, QObject* parent=nullptr);
    QString getName() override;
    bool handle(const QString& text,
                const ParsedIntent& parsedIntent,
                bool& isImmersive) override;
};

#endif // SYSTEMINFO_H
