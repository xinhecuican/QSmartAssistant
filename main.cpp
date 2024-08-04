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
    Robot *robot = new Robot(&a);
    robot->start();
    a.connect(&a, &QCoreApplication::aboutToQuit, &a, [=]() {
        qDebug() << "stop";
        robot->stop();
    });
    return a.exec();
}
#endif
