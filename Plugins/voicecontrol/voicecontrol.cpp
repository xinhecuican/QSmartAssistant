#include "voicecontrol.h"
#include "../../Recorder/player.h"
#include "../../Utils/Utils.h"

VoiceControl::VoiceControl() {}

QString VoiceControl::getName() { return "VoiceControl"; }

void VoiceControl::setPluginHelper(IPluginHelper *helper) {
    this->helper = helper;
}

void VoiceControl::recvMessage(const QString &text,
                               const ParsedIntent &parsedIntent,
                               const PluginMessage &message) {}

bool VoiceControl::handle(const QString &text, const ParsedIntent &parsedIntent, int id,
                          bool &isImmersive) {
    Q_UNUSED(isImmersive)
    int volumeStep = 10;
    if (parsedIntent.hasIntent("CHANGE_VOL")) {
        Intent changeVolIntent = parsedIntent.intents["CHANGE_VOL"];
        bool preciseChange = false;
        int changeValue = 0;
        int changeDir = false;
        for (auto &slot : changeVolIntent.intentSlots) {
            if (slot.name == "user_v") {
                changeValue = chineseToNum(slot.value);
                preciseChange = true;
            } else if (slot.name == "user_d") {
                if (slot.value == "--HIGHER--")
                    changeDir = 1;
                else
                    changeDir = 2;
            } else if (slot.name == "user_vd") {
                if (slot.value == "--LOUDER--")
                    changeDir = 1;
                else
                    changeDir = 2;
            }
        }
        if (preciseChange) {
            if (changeDir) {
                helper->getPlayer()->modifyVolume(
                    changeDir == 1 ? changeValue : -changeValue);
            } else {
                helper->getPlayer()->setVolume(changeValue);
            }
        } else {
            if (changeDir) {
                helper->getPlayer()->modifyVolume(changeDir == 1 ? volumeStep
                                                                 : -volumeStep);
            } else if (text.contains("当前音量")) {
                helper->say("当前音量为" +
                            QString::number(helper->getPlayer()->getVolume()), id);
                return true;
            }
        }
        helper->say("音量为" +
                    QString::number(helper->getPlayer()->getVolume()), id);
        return true;
    }
    return false;
}
