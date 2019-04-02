#include "reprocessorrunner.h"

#include <QCoreApplication>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);    
    ReprocessorRunner runner;
    runner.run();

    return a.exec();
}
