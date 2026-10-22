#pragma once

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPushButton;
class TcpClient;

// 客户端主窗口
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    // TCP 网络通信对象
    TcpClient *m_tcpClient = nullptr;

    // 服务器地址和端口输入框
    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_portEdit = nullptr;

    // 显示连接状态
    QLabel *m_statusLabel = nullptr;

    // 连接和断开按钮
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_disconnectButton = nullptr;
};