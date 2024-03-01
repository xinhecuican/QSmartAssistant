#include <QCoreApplication>
#include "robot.h"
#include <QDebug>
#include <QDir>
#ifdef TEST
#include "Test/tst_sherpa.h"
#include <QTest>
QTEST_MAIN(tst_sherpa);
#else
int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    // 设置PWD
    QString applicationDirPathStr = QCoreApplication::applicationDirPath();
    QDir::setCurrent(applicationDirPathStr);
    Robot* robot = new Robot(&a);
    robot->start();
    a.connect(&a, &QCoreApplication::aboutToQuit, &a, [=]() {
        qDebug() << "stop";
        robot->stop();
    });
    return a.exec();
}
#endif
