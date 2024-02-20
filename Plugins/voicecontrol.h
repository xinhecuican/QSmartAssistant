#ifndef VOICECONTROL_H
#define VOICECONTROL_H
#include "Plugin.h"

class VoiceControl : public Plugin
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit VoiceControl(QObject* parent=nullptr);
    QString getName() override;
    bool handle(const QString& text,
                const ParsedIntent& parsedIntent,
                bool& isImmersive) override;
};

#endif // VOICECONTROL_H
