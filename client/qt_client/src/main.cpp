
#include <QApplication>

#include "ui/LoginWindow.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 程序启动时先显示登录窗口。
    auto *loginWindow = new LoginWindow;
    loginWindow->setAttribute(Qt::WA_DeleteOnClose);
    loginWindow->show();

    // 登录成功后，再创建并显示聊天主窗口。
    QObject::connect(loginWindow, &LoginWindow::loginSucceeded,
                     &app, [loginWindow]()
    {
        auto *mainWindow = new MainWindow;
        mainWindow->setAttribute(Qt::WA_DeleteOnClose);

        // 先显示主窗口，再关闭登录窗口。
        mainWindow->show();
        loginWindow->close();
    });

    return app.exec();
}
