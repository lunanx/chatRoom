#include "MainWindow.h"

#include "network/TcpClient.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_tcpClient(new TcpClient(this))
{
    setWindowTitle("ChatRoom Client");
    resize(520, 280);

    // 创建中央窗口和垂直布局
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // 服务器地址和端口输入区域
    QFormLayout *formLayout = new QFormLayout();

    m_hostEdit = new QLineEdit();
    m_hostEdit->setPlaceholderText("输入服务器 IP 地址");

    m_portEdit = new QLineEdit();
    m_portEdit->setPlaceholderText("输入服务器监听端口");

    formLayout->addRow("服务器地址：", m_hostEdit);
    formLayout->addRow("服务器端口：", m_portEdit);

    mainLayout->addLayout(formLayout);

    // 创建连接和断开按钮
    m_connectButton = new QPushButton("连接服务器");
    m_disconnectButton = new QPushButton("断开连接");

    m_disconnectButton->setEnabled(false);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_connectButton);
    buttonLayout->addWidget(m_disconnectButton);

    mainLayout->addLayout(buttonLayout);

    // 初始化连接状态
    m_statusLabel = new QLabel("未连接服务器");
    mainLayout->addWidget(m_statusLabel);

    setCentralWidget(centralWidget);

    // 点击连接按钮
    connect(m_connectButton, &QPushButton::clicked,
            this, [this]()
    {
        const QString host = m_hostEdit->text().trimmed();

        bool portOk = false;
        const uint port = m_portEdit->text().toUInt(&portOk);

        // 检查地址和端口是否合法
        if (host.isEmpty() ||
            !portOk ||
            port == 0 ||
            port > 65535)
        {
            m_statusLabel->setText("请输入有效的服务器地址和端口");
            return;
        }

        m_statusLabel->setText("正在连接服务器……");
        m_connectButton->setEnabled(false);

        m_tcpClient->connectToServer(
            host, static_cast<quint16>(port));
    });

    // 点击断开连接按钮
    connect(m_disconnectButton, &QPushButton::clicked,
            this, [this]()
    {
        m_tcpClient->disconnectFromServer();
        m_statusLabel->setText("正在断开连接……");
    });

    // TCP 连接成功
    connect(m_tcpClient, &TcpClient::connectedToServer,
            this, [this]()
    {
        m_statusLabel->setText("已连接服务器");
        m_connectButton->setEnabled(false);
        m_disconnectButton->setEnabled(true);
    });

    // TCP 连接断开
    connect(m_tcpClient, &TcpClient::disconnectedFromServer,
            this, [this]()
    {
        m_statusLabel->setText("未连接服务器");
        m_connectButton->setEnabled(true);
        m_disconnectButton->setEnabled(false);
    });

    // TCP 连接或网络操作发生错误
    connect(m_tcpClient, &TcpClient::connectionError,
            this, [this](const QString &message)
    {
        m_statusLabel->setText("网络错误：" + message);
        m_connectButton->setEnabled(!m_tcpClient->isConnected());
        m_disconnectButton->setEnabled(m_tcpClient->isConnected());
    });

    // 收到服务器的数据包
    connect(m_tcpClient, &TcpClient::packetReceived,
            this, [this](quint16 command,
                         quint64 requestId,
                         const QByteArray &jsonBody)
    {
        Q_UNUSED(jsonBody);

        m_statusLabel->setText(
            QString("收到数据包：command=%1，request_id=%2")
                .arg(command)
                .arg(requestId));
    });

    // 收到协议错误
    connect(m_tcpClient, &TcpClient::protocolError,
            this, [this](const QString &message)
    {
        m_statusLabel->setText("协议错误：" + message);
    });
}