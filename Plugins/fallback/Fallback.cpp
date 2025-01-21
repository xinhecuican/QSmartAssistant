#include "Fallback.h"

Fallback::Fallback() {}

QString Fallback::getName() { return "Fallback"; }

void Fallback::setPluginHelper(IPluginHelper *helper) { this->helper = helper; }

bool Fallback::handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                      bool &isImmersive) {
    helper->say("您说的是" + text, id);
    return true;
}

void Fallback::recvMessage(const QString &text,
                           const ParsedIntent &parsedIntent,
                           const PluginMessage &message) {}