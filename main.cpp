#include "fetchdeeznutzwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    FetchDeeznutzWindow w;
    w.show();
    return a.exec();
}
