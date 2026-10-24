#pragma once

#include <QObject>
#include <QByteArray>
#include <QJsonObject>
#include <QQueue>
#include <QString>
#include <QtGlobal>

#include "protocol/ProtocolHeader.h"

// 这里只做前置声明，不在头文件中引入完整窗口和网络类定义。
// 这样能减少头文件之间的相互依赖，加快编译速度。
class LoginWindow;
class MainWindow;
class TcpClient;

// AppController 是客户端应用层的总协调者。
// 它不负责绘制界面，也不自己实现 TCP 协议；它负责把界面操作、网络请求和业务结果连接起来。
class AppController : public QObject
{
    Q_OBJECT

public:
    // parent 默认为 nullptr。main.cpp 中通常在栈上创建 AppController。
    explicit AppController(QObject *parent = nullptr);

    // 释放本控制器创建的顶层窗口。
    // m_tcpClient 的 QObject 父对象是 this，会由 QObject 自动释放。
    ~AppController() override;

private slots:
    // LoginWindow 发出 loginRequested 时调用。
    // account/password 来自登录表单，rememberMe 表示用户是否勾选“记住我”。
    void onLoginRequested(const QString &account,
                          const QString &password,
                          bool rememberMe);

    // LoginWindow 发出 registerRequested 时调用。
    void onRegisterRequested(const QString &account,
                             const QString &password);

    // 下面三个槽处理 TCP 连接状态变化。
    void onConnectedToServer();
    void onDisconnectedFromServer();
    void onConnectionError(const QString &message);

    // 收到 TcpClient 解析好的完整数据包时调用。
    // command 是服务端返回的命令类型，requestId 对应原请求编号，jsonBody 是 JSON 消息体。
    void onPacketReceived(quint16 command,
                          quint64 requestId,
                          const QByteArray &jsonBody);

    // LoginWindow 发出 loginSucceeded 后调用，用于打开聊天主窗口。
    void onLoginSucceeded();

private:
    // 保存一个尚待发送或可以立即发送的请求。
    // TCP 连接是异步的：用户可能在连接完成前就点击登录，因此需要暂存请求。
    struct PendingRequest
    {
        protocolHeader::CommandType command; // 请求类型，例如 LOGIN_REQUEST。
        quint64 requestId;                   // 请求编号；服务器回复时应返回相同编号。
        QJsonObject body;                    // JSON 请求内容，例如 username/password。
    };

    // 读取客户端自己的配置文件 client.conf，得到服务器地址与端口。
    bool loadClientConfig();

    // 如果已经连接就直接发送；如果还没连接就先进入队列，连接成功后再发送。
    void queueOrSendRequest(protocolHeader::CommandType command,
                            const QJsonObject &body);

    // 通过 TcpClient 真正发送一个请求。返回 false 表示发送失败。
    bool sendRequest(const PendingRequest &request);

    // 按顺序发送连接成功前暂存的请求。
    void sendPendingRequests();

    // 在尚未连接且当前没有正在连接时，发起异步连接。
    void ensureServerConnection();

    // 统一显示错误或普通提示，避免每个槽都重复编写 QMessageBox 代码。
    void showResponseMessage(const QString &title,
                             const QString &message,
                             bool isError);

private:
    // 网络对象：真正管理 QTcpSocket、协议编码和数据收发。
    // 将 this 作为父对象，AppController 销毁时 Qt 会自动销毁它。
    TcpClient *m_tcpClient = nullptr;

    // 登录窗口和聊天窗口都是顶层窗口，因此不通过窗口父子关系管理生命周期。
    // 这里保存指针，方便切换页面以及在控制器析构时释放。
    LoginWindow *m_loginWindow = nullptr;
    MainWindow *m_mainWindow = nullptr;

    // 从 client.conf 读取的服务器配置。
    QString m_serverHost;
    quint16 m_serverPort;

    // 防止短时间内重复调用 connectToServer。
    bool m_isConnecting = false;

    // 下一个请求编号从 1 开始递增，便于将响应与请求对应起来。
    quint64 m_nextRequestId = 1;

    // 在 TCP 连接建立之前，先把用户请求放在这里等待发送。
    QQueue<PendingRequest> m_pendingRequests;
};
