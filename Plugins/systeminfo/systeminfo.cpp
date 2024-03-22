#include "systeminfo.h"
#include <QFile>
#include <QProcess>

SystemInfo::SystemInfo() {}

QString SystemInfo::getName() { return "SystemInfo"; }

void SystemInfo::setPluginHelper(IPluginHelper *helper) {
    this->helper = helper;
}

void SystemInfo::recvMessage(const QString &text,
                             const ParsedIntent &parsedIntent,
                             const PluginMessage &message) {}

bool SystemInfo::handle(const QString &text, const ParsedIntent &parsedIntent,
                        bool &isImmersive) {
    Q_UNUSED(text)
    Q_UNUSED(isImmersive)
    for (auto &intent : parsedIntent.intents) {
        if (intent.name == "SYS_INFO") {
            QFile temperature("/sys/class/thermal/thermal_zone0/temp");
            float temp = 0;
            if (!temperature.open(QIODevice::ReadOnly)) {
                qWarning() << "can't read cpu temp";
            } else {
                temp = temperature.readAll().toInt() / 1000.;
            }
            QProcess memUsage;
            memUsage.start("free", {"-m"});
            memUsage.waitForFinished(2000);
            QString memInfo = memUsage.readAllStandardOutput();
            QString memSplit = memInfo.split('\n').at(1);
            int mem = 0;
            int currentNumber = 1;
            bool containsNumber = false;
            for (int i = memSplit.size() - 1; i >= 0; i--) {
                if (memSplit[i] >= '0' && memSplit[i] <= '9') {
                    containsNumber = true;
                    mem += currentNumber * (memSplit[i].toLatin1() - '0');
                    currentNumber *= 10;
                } else if (containsNumber) {
                    break;
                }
            }
            helper->say("内核温度为" + QString::number(temp));
            if ((mem / 1024) == 0) {
                helper->say("剩余内存为" + QString::number(mem) + "兆");
            } else {
                QString memG = QString::number(mem / 1024);
                QString memM = QString::number(mem % 1024);
                helper->say("剩余内存为" + memG + "吉" + memM + "兆");
            }
            return true;
        }
    }
    return false;
}
