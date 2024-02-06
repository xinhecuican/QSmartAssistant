#include <QCoreApplication>
#include "robot.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    Robot* robot = new Robot(&a);
    robot->start();
    return a.exec();
}
