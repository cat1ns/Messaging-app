#include "Application/DataReceiveServer.h"
#include "Authentication/UserLogin.h"

#include <QtWidgets/QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    UserLogin loginDlg;
    if (loginDlg.exec() != QDialog::Accepted)
        return 0;

    DataReceiveServer window;
    window.show();
    return app.exec();
}
