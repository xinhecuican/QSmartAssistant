#include "robot.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#ifdef TEST
#include "Test/tst_sherpa.h"
#include <QTest>
QTEST_GUILESS_MAIN(tst_sherpa);
#else
#ifdef BREAKPAD
#include "client/linux/handler/exception_handler.h"
#include "Utils/config.h"

static bool dumpCallback(const google_breakpad::MinidumpDescriptor &descriptor,
                         void *context, bool succeeded) {
    qInfo() << "Dump path:" << descriptor.path();
    return succeeded;
}
#endif

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);
    // 设置PWD
    QString applicationDirPathStr = QCoreApplication::applicationDirPath();
    QDir::setCurrent(applicationDirPathStr);
#ifdef BREAKPAD
    google_breakpad::MinidumpDescriptor descriptor(
        Config::getDataPath("Tmp").toStdString()); // minidump文件写入到的目录
    google_breakpad::ExceptionHandler eh(descriptor, NULL, dumpCallback, NULL,
                                         true, -1);
#endif
    QCoreApplication::addLibraryPath(QDir::homePath() +
                                     "/.config/QSmartAssistant/plugins/lib");
    // ⚠ robot 必须是**局部对象**，不要 new 到 a 上。挂在 QCoreApplication
    //   底下的对象是在 ~QObject 里被删的，而 ~QCoreApplication 的函数体早已
    //   经清掉了主线程的 QThreadData / 事件派发器 —— 插件里的 QProcess /
    //   QTimer / QNetworkAccessManager 在那个时机析构就会崩（实测退出码
    //   0xC0000409），细节见 Plugins/pluginmanager.h 的析构函数说明。
    //   局部对象在 a.exec() 之后、a 之前析构，正好落在 Qt 还活着的时候。
    Robot robot;
    robot.start();
    a.connect(&a, &QCoreApplication::aboutToQuit, &a, [&robot]() {
        qDebug() << "stop";
        robot.stop();
    });
    return a.exec();
}
#endif
