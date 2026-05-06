#include "BadSectorCheck.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    BadSectorCheck window;
    window.show();
    return app.exec();
}
