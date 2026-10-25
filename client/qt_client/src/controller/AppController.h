#pragma once

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QQueue>
#include <QString>
#include <QTimer>
#include <QtGlobal>

#include "protocol/ProtocolHeader.h"

class LoginWindow;
class MainWindow;
class TcpClient;

// AppController 负责协调登录状态、窗口生命周期和网络请求。
// 窗口只负责显示界面并发出信号，TCP 细节则交由 TcpClient 处理。
class AppController : public QObject
{
    Q_OBJECT

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

private slots:
    void onLoginRequested(const QString &account,
                          const QString &password,
                          bool rememberMe);
    void onRegisterRequested(const QString &account,
                             const QString &password);

    void onConnectedToServer();
    void onDisconnectedFromServer();
    void onConnectionError(const QString &message);
    void onPacketReceived(quint16 command,
                          quint64 requestId,
                          const QByteArray &jsonBody);

    // 主窗口通过侧边栏菜单发出退出登录请求。
    void onLogoutRequested();
    void onLoginSucceeded();

    // 当前已登录用户在选中的真实会话中发送一条文本消息。
    void onMessageSendRequested(const QString &text, quint64 conversationId, bool isGroup);

private:
    // 一个请求在真正发送前需要保存命令、编号和 JSON 内容。
    // 连接建立前的请求先进入队列，连接成功后再按顺序发送。
    struct PendingRequest
    {
        protocolHeader::CommandType command;
        quint64 requestId;
        QJsonObject body;
    };

    bool loadClientConfig();
    void queueOrSendRequest(protocolHeader::CommandType command,
                            const QJsonObject &body);
    bool sendRequest(const PendingRequest &request);
    void sendPendingRequests();
    void ensureServerConnection();

    // 清理本次尚未完成的网络请求状态，不会自行更改已登录用户。
    void clearPendingRequests();

    // 清除客户端当前的登录信息，并将界面恢复到登录窗口。
    void returnToLoginWindow();

    // 停止指定消息请求的超时计时器，或清理所有消息请求计时器。
    // 服务端消息推送使用 request_id=0，不属于任何待处理请求，需要单独解析。
    void handleIncomingMessagePush(protocolHeader::CommandType command,
                                   quint64 requestId,
                                   const QByteArray &jsonBody);

    void stopMessageRequestTimer(quint64 requestId);
    void clearMessageRequestTimers();

    // 统一显示紫灰主题提示框。
    void showResponseMessage(const QString &title,
                             const QString &message,
                             bool isError);

private:
    TcpClient *m_tcpClient = nullptr;
    LoginWindow *m_loginWindow = nullptr;
    MainWindow *m_mainWindow = nullptr;

    QString m_serverHost;
    quint16 m_serverPort = 0;
    bool m_configurationValid = false;
    bool m_isConnecting = false;

    quint64 m_nextRequestId = 1;
    QQueue<PendingRequest> m_pendingRequests;
    QHash<quint64, protocolHeader::CommandType> m_requestsAwaitingResponse;

    // 认证请求处理中时禁止再次提交，避免快速双击产生多个登录/注册请求。
    bool m_authRequestPending = false;

    // 退出登录请求在等待服务器确认时为 true。
    bool m_isLoggingOut = false;

    // 当前是否已经通过服务器认证。不能只根据“点击过登录按钮”来判断。
    bool m_isAuthenticated = false;

    // 登录请求发出时暂存账号；只有收到 LOGIN_SUCCESS 才会把它记为当前账号。
    QString m_pendingLoginAccount;
    QString m_currentAccount;

    // 登录、注册、退出登录都设置超时，避免界面一直停留在等待状态。
    QTimer m_requestTimer;

    // 消息可以连续发送，单独为每条消息计时，避免一个响应重置其他消息的超时。
    QHash<quint64, QTimer *> m_messageRequestTimers;
};
