#include "systeminfo.h"
#include <QProcess>
#include "PluginReflector.h"
REG_CLASS(SystemInfo)

SystemInfo::SystemInfo(IPluginHelper* helper, QObject* parent)
    :Plugin(helper, parent){

}

QString SystemInfo::getName(){
    return "SystemInfo";
}

bool SystemInfo::handle(const QString& text,
                        const ParsedIntent& parsedIntent,
                        bool& isImmersive){
    Q_UNUSED(text)
    Q_UNUSED(isImmersive)
    for(auto& intent : parsedIntent.intents){
        if(intent.name == "SYS_INFO"){
            QProcess cpuUsage1;
            cpuUsage1.start("top -n1");
            cpuUsage1.waitForFinished(2000);
            cpuUsage1.kill();
            QString cpu = cpuUsage1.readAllStandardOutput();
            QRegExp cpuRegex(".*Cpu(s):.*(\\d+\\.\\d*).*");
            cpuRegex.indexIn(cpu);
            cpu = cpuRegex.cap(1);
            QProcess memUsage;
            memUsage.start("free -m");
            memUsage.waitForFinished(2000);
            QString memInfo = memUsage.readAllStandardOutput();
            QString memSplit = memInfo.split('\n').at(1);
            int mem = 0;
            int currentNumber = 1;
            bool containsNumber = false;
            for(int i=memSplit.size()-1; i>=0; i--){
                if(memSplit[i] >= '0' && memSplit[i] <= '9'){
                    containsNumber = true;
                    mem += currentNumber * (memSplit[i].toLatin1() - '0');
                    currentNumber *= 10;
                }
                else if(containsNumber){
                    break;
                }
            }
            helper->say("cpu利用率为" + cpu);
            if((mem / 1024) == 0){
                helper->say("剩余内存为" + QString::number(mem) + "M");
            }
            else{
                QString memG = QString::number(mem / 1024);
                QString memM = QString::number(mem % 1024);
                helper->say("剩余内存为" + memG + "G" + memM + "M");
            }
            return true;
        }
    }
    return false;
}
