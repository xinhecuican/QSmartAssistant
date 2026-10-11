#ifndef RASANLU_H
#define RASANLU_H
#include "nlumodel.h"
#include <QtNetwork>
// 下面用到了 QProcess / QStringList / QObject。原先只 include 了 <QtNetwork>
// （靠它间接带进来的），这属于依赖传递性 —— 换 Qt 版本或调整 include 顺序就
// 可能编不过。这里显式声明用到什么。
#include <QProcess>
#include <QStringList>

class RasaNLU : public NLUModel {
public:
    RasaNLU(QObject *parent = nullptr);
    ParsedIntent parseIntent(const QString &text) override;
    void stop() override;

private:
    Intent entity2Slot(const Intent &entityIntent);
#if defined(Q_OS_WIN)
    // Windows 没有 start-rasa.sh / stop-rasa.sh，直接在进程内拉起 venv 里的
    // rasa.exe（见 doc/windows.md 的 rasa 章节）。
    // 入参 rasaExe 已由 resolveRasaVenv() 解析为 rasa.exe 的绝对路径。
    void startRasa(const QString &rasaExe, const QString &model);
    void stopRasa();
#else
    // POSIX 下统一调 Data/<script>。args 为脚本参数：
    //   start-rasa.sh <activate脚本> [模型路径]
    //   stop-rasa.sh
    void runRasash(const QString &script, const QStringList &args);
#endif

private:
    QNetworkAccessManager manager;
    QNetworkRequest request;
    QProcess process;
    bool recordSamples;
	float conf;
};

#endif // RASANLU_H
