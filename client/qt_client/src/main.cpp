#include <QApplication>

#include "controller/AppController.h"

int main(int argc, char *argv[])
{
    // QApplication 管理整个 Qt Widgets 应用，包括窗口、事件和用户输入。
    QApplication app(argc, argv);

    // 入口函数只创建总控制器，不直接创建 LoginWindow/MainWindow。
    // AppController 会负责显示登录窗口、发送网络请求，并在登录成功后切换到聊天主窗口。
    AppController appController;

    // 启动 Qt 事件循环。窗口事件、TCP 收发和信号槽都依赖这个循环运行。
    // 当事件循环结束，main 返回，局部对象 appController 随之析构。
    return app.exec();
}
