#include "quitrobot.h"

QuitRobot::QuitRobot() {}

QString QuitRobot::getName() { return "QuitRobot"; }

void QuitRobot::setPluginHelper(IPluginHelper *helper) {
    this->helper = helper;
}

void QuitRobot::recvMessage(const QString &text,
                            const ParsedIntent &parsedIntent,
                            const PluginMessage &message) {}

bool QuitRobot::handle(const QString &text, const ParsedIntent &parsedIntent,
                       bool &isImmersive) {
    Q_UNUSED(text)
    Q_UNUSED(isImmersive)
    if (parsedIntent.hasIntent("QUIT_ROBOT") ||
        parsedIntent.hasIntent("CLOSE_MUSIC")) {
        QString answer = helper->question("是否退出");
        if (answer == "是" || answer == "确定") {
            helper->exit();
        }
        return true;
    }
    return false;
}
