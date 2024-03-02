#include "quitrobot.h"
#include "PluginReflector.h"
REG_CLASS(QuitRobot)

QuitRobot::QuitRobot(IPluginHelper* helper, QObject* parent)
    : Plugin(helper, parent){
}

QString QuitRobot::getName(){
    return "QuitRobot";
}

bool QuitRobot::handle(const QString& text,
                          const ParsedIntent& parsedIntent,
                          bool& isImmersive){
    Q_UNUSED(text)
    Q_UNUSED(isImmersive)
    if(parsedIntent.hasIntent("QUIT_ROBOT") || parsedIntent.hasIntent("CLOSE_MUSIC")){
        QString answer = helper->question("是否退出");
        if(answer == "是" || answer == "确定"){
            helper->exit();
        }
    }
    return false;
}
