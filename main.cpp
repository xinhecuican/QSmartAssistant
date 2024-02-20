#include <QCoreApplication>
#include "robot.h"
#include <QDebug>
#ifdef TEST
#include "Test/tst_sherpa.h"
#include <QTest>
QTEST_MAIN(tst_sherpa);
#else
int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    Robot* robot = new Robot(&a);
    robot->start();
    a.connect(&a, &QCoreApplication::aboutToQuit, &a, [=]() {
        qDebug() << "stop";
        robot->stop();
    });
    return a.exec();
}
#endif
