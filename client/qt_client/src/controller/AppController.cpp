#include "AppController.h"

#include "network/TcpClient.h"
#include "ui/LoginWindow.h"
#include "ui/MainWindow.h"
#include "ui/ModernMessageBox.h"
#include "protocol/ProtocolBody.h"
#include "model/ChatMessageRecord.h"

#include <QCoreApplication>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>
#include "Theme.h"
#include "ui/SessionListPanel.h"
#include <QDateTime>
#include <QGraphicsDropShadowEffect>
#include <QFrame>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QWidget>
#include <QVector>
#include <QUuid>

namespace
{
    // 当前服务器以 JSON 整数发送 ID 和 seq。toInteger() 只接受可以无损表示的整数，
    // 避免先转成 double 后再强转整数时，悄悄改变较大的数据库 ID。
    bool readPositiveJsonInteger(const QJsonObject &object,
                                 const char *key,
                                 quint64 &result)
    {
        const QJsonValue value = object.value(QString::fromLatin1(key));
        if (!value.isDouble())
        {
            return false;
        }

        const qint64 number = value.toInteger(0);
        if (number <= 0)
        {
            return false;
        }

        result = static_cast<quint64>(number);
        return result > 0;
    }

    QString displayConversationTime(const QString &serverTime)
    {
        const QDateTime time = QDateTime::fromString(
            serverTime, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!time.isValid())
        {
            return serverTime;
        }
        if (time.date() == QDate::currentDate())
        {
            return time.toString(QStringLiteral("HH:mm"));
        }
        if (time.date().year() == QDate::currentDate().year())
        {
            return time.toString(QStringLiteral("MM/dd"));
        }
        return time.toString(QStringLiteral("yy/MM/dd"));
    }
}

AppController::AppController(QObject *parent)
    : QObject(parent),
      // TcpClient 是普通 QObject 子对象，由 AppController 自动管理其生命周期。
      m_tcpClient(new TcpClient(this)),
      // LoginWindow 是顶层窗口，不能设置成 AppController 的 QWidget 子窗口。
      m_loginWindow(new LoginWindow)
{
    // 所有需要等待服务器结果的操作共用一个单次计时器。
    // 超时后结束当前请求，避免界面永久卡在“请求处理中”。
    m_requestTimer.setSingleShot(true);
    connect(&m_requestTimer, &QTimer::timeout, this, [this]()
    {
        const bool logoutTimedOut = m_isLoggingOut;
        const bool authTimedOut = m_authRequestPending;

        clearPendingRequests();
        m_pendingLoginAccount.clear();

        if (logoutTimedOut)
        {
            // 退出请求超时意味着客户端无法确认服务端是否完成退出。
            // 断开 TCP 连接可让服务端清理当前连接对应的会话，客户端随后回到登录页。
            m_isAuthenticated = false;
            m_currentAccount.clear();
            returnToLoginWindow();
            setNetworkStatus(QStringLiteral("退出登录超时 · 连接已重置"), QStringLiteral("disconnected"));
            m_tcpClient->abortConnection();
            showResponseMessage(
                QStringLiteral("退出登录超时"),
                QStringLiteral("暂时无法确认服务器是否完成退出登录。客户端已返回登录界面，请重新登录。"),
                true);
        }
        else if (authTimedOut)
        {
            // 登录请求可能已被服务器处理，只是响应没有到达客户端。
            // 主动断开连接，避免服务端已登录而客户端仍停留在登录页的状态不一致。
            m_tcpClient->abortConnection();
            setNetworkStatus(QStringLiteral("服务器响应超时 · 请检查网络"), QStringLiteral("error"));
            showResponseMessage(
                QStringLiteral("服务器响应超时"),
                QStringLiteral("服务器暂时没有返回登录/注册结果，请检查服务端后重试。"),
                true);
        }
    });

    // 登录窗口只采集用户输入；所有业务处理都交由 AppController。
    connect(m_loginWindow, &LoginWindow::loginRequested,
            this, &AppController::onLoginRequested);
    connect(m_loginWindow, &LoginWindow::registerRequested,
            this, &AppController::onRegisterRequested);
    connect(m_loginWindow, &LoginWindow::loginSucceeded,
            this, &AppController::onLoginSucceeded);

    // 监听 TCP 连接状态。
    connect(m_tcpClient, &TcpClient::connectedToServer,
            this, &AppController::onConnectedToServer);
    connect(m_tcpClient, &TcpClient::disconnectedFromServer,
            this, &AppController::onDisconnectedFromServer);
    connect(m_tcpClient, &TcpClient::connectionError,
            this, &AppController::onConnectionError);

    // 协议错误意味着当前数据包无法安全处理。清理请求状态，防止一直等待响应。
    connect(m_tcpClient, &TcpClient::protocolError,
            this, [this](const QString &message)
    {
        const bool hadSession = m_isAuthenticated || m_isLoggingOut;
        clearPendingRequests();
        m_pendingLoginAccount.clear();

        if (hadSession)
        {
            m_isAuthenticated = false;
            m_currentAccount.clear();
            returnToLoginWindow();
            setNetworkStatus(QStringLiteral("通信异常 · 请重新登录"), QStringLiteral("error"));
        }
        else
        {
            setNetworkStatus(QStringLiteral("通信协议异常"), QStringLiteral("error"));
        }

        showResponseMessage(QStringLiteral("通信协议错误"), message, true);
    });

    // TcpClient 已负责 TCP 拆包，这里收到的是一整条协议消息。
    connect(m_tcpClient, &TcpClient::packetReceived,
            this, &AppController::onPacketReceived);

    // 启动时显示登录窗口，但不主动连接服务器。
    m_loginWindow->show();
    m_configurationValid = loadClientConfig();
}

AppController::~AppController()
{
    // 两个窗口都是独立顶层窗口，需由控制器手动释放。
    delete m_mainWindow;
    delete m_loginWindow;
    // m_tcpClient 以 this 为父对象，会由 QObject 自动释放。
}

bool AppController::loadClientConfig()
{
    // 配置文件从 exe 所在目录读取；请修改运行目录下实际使用的 client.conf。
    const QString configPath =
        QCoreApplication::applicationDirPath() + QStringLiteral("/client.conf");
    QSettings settings(configPath, QSettings::IniFormat);

    m_serverHost = settings.value(
        QStringLiteral("SERVER_HOST"),
        QStringLiteral("192.168.230.128")).toString().trimmed();

    bool portOk = false;
    const int port = settings.value(QStringLiteral("SERVER_PORT"), 8888).toInt(&portOk);

    if (m_serverHost.isEmpty() || !portOk || port < 1 || port > 65535)
    {
        showResponseMessage(
            QStringLiteral("客户端配置错误"),
            QStringLiteral("client.conf 中的 SERVER_HOST 或 SERVER_PORT 无效。"),
            true);
        return false;
    }

    m_serverPort = static_cast<quint16>(port);
    return true;
}

void AppController::onLoginRequested(const QString &account,
                                     const QString &password,
                                     bool rememberMe)
{
    Q_UNUSED(rememberMe);

    if (!m_configurationValid)
    {
        showResponseMessage(QStringLiteral("无法登录"),
                            QStringLiteral("请先检查客户端配置文件 client.conf。"), true);
        return;
    }

    if (m_isAuthenticated)
    {
        showResponseMessage(QStringLiteral("当前已登录"),
                            QStringLiteral("当前账号已经登录，请先退出登录再切换账号。"), false);
        return;
    }

    if (m_authRequestPending || m_isLoggingOut)
    {
        showResponseMessage(QStringLiteral("请求处理中"),
                            QStringLiteral("当前请求正在处理中，请稍候。"), false);
        return;
    }

    const QString normalizedAccount = account.trimmed();
    if (normalizedAccount.isEmpty() || password.isEmpty())
    {
        showResponseMessage(QStringLiteral("无法登录"),
                            QStringLiteral("请输入账号和密码。"), true);
        return;
    }

    QJsonObject body;
    // 当前服务端读取 username；account 是界面语义字段，作为兼容字段一并保留。
    body.insert(QStringLiteral("account"), normalizedAccount);
    body.insert(QString::fromLatin1(protocolBody::USERNAME), normalizedAccount);
    body.insert(QString::fromLatin1(protocolBody::PASSWORD), password);

    m_pendingLoginAccount = normalizedAccount;
    m_pendingRememberMe = rememberMe;
    m_authRequestPending = true;
    m_requestTimer.start(15000);
    queueOrSendRequest(protocolHeader::CommandType::LOGIN_REQUEST, body);
}

void AppController::onRegisterRequested(const QString &account,
                                        const QString &password)
{
    if (!m_configurationValid)
    {
        showResponseMessage(QStringLiteral("无法注册"),
                            QStringLiteral("请先检查客户端配置文件 client.conf。"), true);
        return;
    }

    if (m_isAuthenticated)
    {
        showResponseMessage(QStringLiteral("当前已登录"),
                            QStringLiteral("当前账号已经登录，请先退出登录再注册或切换账号。"), false);
        return;
    }

    if (m_authRequestPending || m_isLoggingOut)
    {
        showResponseMessage(QStringLiteral("请求处理中"),
                            QStringLiteral("当前请求正在处理中，请稍候。"), false);
        return;
    }

    const QString normalizedAccount = account.trimmed();
    if (normalizedAccount.isEmpty() || password.isEmpty())
    {
        showResponseMessage(QStringLiteral("无法注册"),
                            QStringLiteral("请输入账号和密码。"), true);
        return;
    }

    m_pendingRememberMe = false;

    QJsonObject body;
    body.insert(QStringLiteral("account"), normalizedAccount);
    body.insert(QString::fromLatin1(protocolBody::USERNAME), normalizedAccount);
    body.insert(QString::fromLatin1(protocolBody::PASSWORD), password);
    // 当前注册表单还没有单独的昵称输入项，暂时以账号作为初始昵称。
    body.insert(QString::fromLatin1(protocolBody::NICKNAME), normalizedAccount);

    m_authRequestPending = true;
    m_requestTimer.start(15000);
    queueOrSendRequest(protocolHeader::CommandType::REGISTER_REQUEST, body);
}

void AppController::queueOrSendRequest(protocolHeader::CommandType command,
                                       const QJsonObject &body)
{
    const PendingRequest request{command, m_nextRequestId++, body};

    if (m_tcpClient->isConnected())
    {
        sendRequest(request);
        return;
    }

    // TCP 连接建立是异步的；连接成功前先保存请求，避免用户点击太快导致请求丢失。
    m_pendingRequests.enqueue(request);
    ensureServerConnection();
}

bool AppController::sendRequest(const PendingRequest &request)
{
    if (!m_tcpClient->sendRequest(request.command, request.requestId, request.body))
    {
        // TcpClient 会发出 connectionError/protocolError，具体提示由相应槽函数统一处理。
        clearPendingRequests();
        m_pendingLoginAccount.clear();
        return false;
    }

    // 只跟踪成功交给 TcpClient 发送的请求，服务器响应时用 requestId 找回原请求类型。
    m_requestsAwaitingResponse.insert(request.requestId, request.command);
    return true;
}

void AppController::ensureServerConnection()
{
    if (m_tcpClient->isConnected())
    {
        setNetworkStatus(QStringLiteral("服务器已连接"), QStringLiteral("connected"));
        return;
    }
    if (m_isConnecting || m_serverHost.isEmpty())
    {
        return;
    }

    m_isConnecting = true;
    setNetworkStatus(QStringLiteral("正在连接服务器…"), QStringLiteral("connecting"));
    m_tcpClient->connectToServer(m_serverHost, m_serverPort);
}

void AppController::onConnectedToServer()
{
    m_isConnecting = false;
    setNetworkStatus(QStringLiteral("已连接服务器，正在验证账号…"), QStringLiteral("connected"));
    sendPendingRequests();
}

void AppController::onDisconnectedFromServer()
{
    m_isConnecting = false;

    const bool wasAuthenticationPending = m_authRequestPending;
    const bool wasLoggingOut = m_isLoggingOut;
    const bool wasAuthenticated = m_isAuthenticated;

    clearPendingRequests();
    m_pendingLoginAccount.clear();

    if (wasAuthenticated || wasLoggingOut)
    {
        // 已登录期间连接断开，服务端连接会话也随之失效，客户端必须退回登录界面。
        m_isAuthenticated = false;
        m_currentAccount.clear();
        returnToLoginWindow();
        setNetworkStatus(QStringLiteral("连接已断开 · 请重新登录"), QStringLiteral("disconnected"));
        showResponseMessage(
            QStringLiteral("连接已断开"),
            QStringLiteral("与服务器的连接已断开，已返回登录界面，请重新登录。"),
            true);
    }
    else if (wasAuthenticationPending)
    {
        setNetworkStatus(QStringLiteral("连接已断开 · 请检查网络后重试"), QStringLiteral("disconnected"));
        showResponseMessage(
            QStringLiteral("连接已断开"),
            QStringLiteral("请求完成前连接已断开，请检查网络后重试。"),
            true);
    }
    else if (m_networkStatusState == QStringLiteral("connected") ||
             m_networkStatusState == QStringLiteral("connecting"))
    {
        // 若前面的错误处理已经设置了更具体的错误文案，不用普通断开事件覆盖它。
        setNetworkStatus(QStringLiteral("尚未连接服务器"), QStringLiteral("disconnected"));
    }
}

void AppController::onConnectionError(const QString &message)
{
    m_isConnecting = false;

    const bool hadSession = m_isAuthenticated || m_isLoggingOut;
    const bool hadPendingRequest = m_authRequestPending || !m_pendingRequests.isEmpty();

    clearPendingRequests();
    m_pendingLoginAccount.clear();

    if (hadSession)
    {
        m_isAuthenticated = false;
        m_currentAccount.clear();
        returnToLoginWindow();
        setNetworkStatus(QStringLiteral("连接已断开 · 请重新登录"), QStringLiteral("disconnected"));
        m_tcpClient->abortConnection();
        showResponseMessage(
            QStringLiteral("连接已断开"),
            message + QStringLiteral("\n\n客户端已返回登录界面，请重新登录。"),
            true);
    }
    else if (hadPendingRequest)
    {
        setNetworkStatus(QStringLiteral("连接失败 · 请检查地址、端口和服务端"), QStringLiteral("error"));
        showResponseMessage(
            QStringLiteral("服务器连接失败"),
            message + QStringLiteral("\n\n请检查 client.conf 中的 SERVER_HOST 和 SERVER_PORT，以及服务端是否已启动。"),
            true);
    }
}

void AppController::sendPendingRequests()
{
    while (m_tcpClient->isConnected() && !m_pendingRequests.isEmpty())
    {
        const PendingRequest request = m_pendingRequests.dequeue();
        if (!sendRequest(request))
        {
            break;
        }
    }
}

void AppController::onPacketReceived(quint16 command,
                                     quint64 requestId,
                                     const QByteArray &jsonBody)
{
    const auto receivedCommand = static_cast<protocolHeader::CommandType>(command);

    // 历史消息是普通 request/response，但有独立分页状态，不能进入登录响应分支。
    if (m_historyRequests.contains(requestId))
    {
        handleHistoryResponse(requestId, receivedCommand, jsonBody);
        return;
    }

    // 当前服务器主动推送消息时固定使用 request_id=0。
    // 必须先识别推送，再查找请求表；否则这类消息会被误当成“未知响应”直接丢弃。
    if (receivedCommand == protocolHeader::CommandType::GROUP_MESSAGE_RECV ||
        receivedCommand == protocolHeader::CommandType::PRIVATE_MESSAGE_RECV)
    {
        handleIncomingMessagePush(receivedCommand, requestId, jsonBody);
        return;
    }

    auto requestIt = m_requestsAwaitingResponse.find(requestId);
    if (requestIt == m_requestsAwaitingResponse.end())
    {
        return;
    }

    const protocolHeader::CommandType originalCommand = requestIt.value();
    m_requestsAwaitingResponse.erase(requestIt);
    stopOperationRequestTimer(requestId);

    const auto responseCommand = static_cast<protocolHeader::CommandType>(command);

    if (originalCommand == protocolHeader::CommandType::QUERY_CONVERSATION_LIST_REQUEST)
    {
        if (m_conversationListRequestId == requestId)
        {
            m_conversationListRequestId = 0;
        }
        // 先取出合并刷新标记；新响应处理失败时也不能让旧的目标会话 ID 永久残留。
        const bool refreshAgain = m_conversationListRefreshPending;
        m_conversationListRefreshPending = false;
        handleConversationListResponse(responseCommand, jsonBody);
        if (refreshAgain)
        {
            QTimer::singleShot(250, this, [this]() { requestConversationList(); });
        }
        else
        {
            // 若成功打开的会话未出现在最终列表中，清除待选 ID，防止后续刷新误选旧目标。
            m_pendingConversationSelectionId = 0;
        }
        return;
    }

    if (originalCommand == protocolHeader::CommandType::QUERY_FRIEND_LIST_REQUEST)
    {
        if (m_friendListRequestId == requestId)
        {
            m_friendListRequestId = 0;
        }
        handleFriendListResponse(responseCommand, jsonBody);
        return;
    }

    if (originalCommand == protocolHeader::CommandType::QUERY_PENDING_FRIEND_LIST_REQUEST)
    {
        if (m_pendingFriendListRequestId == requestId)
        {
            m_pendingFriendListRequestId = 0;
        }
        handlePendingFriendListResponse(responseCommand, jsonBody);
        return;
    }

    if (originalCommand == protocolHeader::CommandType::FRIEND_REQUEST_SEND)
    {
        if (m_sendFriendRequestId == requestId)
        {
            m_sendFriendRequestId = 0;
        }
        handleFriendRequestSendResponse(responseCommand, jsonBody);
        return;
    }

    if (originalCommand == protocolHeader::CommandType::FRIEND_REQUEST_HANDLE_REQUEST)
    {
        if (m_handleFriendRequestId == requestId)
        {
            m_handleFriendRequestId = 0;
        }
        handleFriendRequestHandleResponse(responseCommand, jsonBody);
        return;
    }

    if (originalCommand == protocolHeader::CommandType::CHATCREATE_REQUEST)
    {
        if (m_createConversationRequestId == requestId)
        {
            m_createConversationRequestId = 0;
        }
        handleCreateConversationResponse(responseCommand, jsonBody);
        return;
    }

    const bool isMessageSendRequest =
        originalCommand == protocolHeader::CommandType::GROUP_MESSAGE_SEND ||
        originalCommand == protocolHeader::CommandType::PRIVATE_MESSAGE_SEND;
    if (isMessageSendRequest)
    {
        const QString clientMessageId = m_messageRequestClientIds.take(requestId);
        m_messagesInFlight.remove(clientMessageId);
        stopMessageRequestTimer(requestId);

        const auto markMessageFailed = [this, &clientMessageId]()
        {
            if (m_mainWindow != nullptr && !clientMessageId.isEmpty())
            {
                m_mainWindow->setMessageSendState(
                    clientMessageId, MessageSendState::Failed);
            }
        };

        if (responseCommand != protocolHeader::CommandType::MESSAGE_SEND_SUCCESS &&
            responseCommand != protocolHeader::CommandType::MESSAGE_SEND_FAILED)
        {
            markMessageFailed();
            if (m_mainWindow != nullptr)
            {
                m_mainWindow->setMessageStatus(
                    QStringLiteral("服务器返回了无法识别的消息发送响应"), true);
            }
            return;
        }

        QJsonParseError messageParseError;
        const QJsonDocument messageDocument =
            QJsonDocument::fromJson(jsonBody, &messageParseError);
        if (messageParseError.error != QJsonParseError::NoError || !messageDocument.isObject())
        {
            markMessageFailed();
            if (m_mainWindow != nullptr)
            {
                m_mainWindow->setMessageStatus(
                    QStringLiteral("无法解析服务器的消息发送结果"), true);
            }
            return;
        }

        const QJsonObject messageBody = messageDocument.object();
        const QString resultMessage = messageBody
            .value(QString::fromLatin1(protocolBody::MESSAGE))
            .toString(QStringLiteral("服务器已返回消息发送结果"));

        if (m_mainWindow != nullptr)
        {
            if (responseCommand == protocolHeader::CommandType::MESSAGE_SEND_SUCCESS)
            {
                // 老协议把 server_msg_id/seq 放在 JSON 最外层；若将来放进 data，
                // 也兼容读取 data 对象，但不强迫服务端迁移旧响应格式。
                QJsonObject acknowledgement = messageBody;
                const QJsonValue dataValue = acknowledgement.value(QString::fromLatin1(protocolBody::DATA));
                if (dataValue.isObject())
                {
                    acknowledgement = dataValue.toObject();
                }

                quint64 acknowledgedServerId = 0;
                quint64 acknowledgedSequence = 0;
                const QString acknowledgedClientId = acknowledgement
                    .value(QString::fromLatin1(protocolBody::CLIENT_MSG_ID)).toString();
                if (!clientMessageId.isEmpty() && acknowledgedClientId == clientMessageId &&
                    readPositiveJsonInteger(acknowledgement, protocolBody::SERVER_MSG_ID,
                                            acknowledgedServerId) &&
                    readPositiveJsonInteger(acknowledgement, protocolBody::SEQ,
                                            acknowledgedSequence))
                {
                    m_mainWindow->confirmPendingMessage(
                        clientMessageId, acknowledgedServerId, acknowledgedSequence);
                }
                else if (!clientMessageId.isEmpty())
                {
                    m_mainWindow->setMessageSendState(
                        clientMessageId, MessageSendState::Sent);
                }

                m_retryableMessages.remove(clientMessageId);
                m_mainWindow->setMessageStatus(QStringLiteral("消息已保存到服务器"), false);
                // 服务端已持久化最新消息，刷新列表摘要和最近活动时间。
                requestConversationList();
            }
            else
            {
                markMessageFailed();
                m_mainWindow->setMessageStatus(
                    resultMessage + QStringLiteral("；点击失败消息可重试"), true);
            }
        }
        return;
    }

    const bool isLoginResponse =
        originalCommand == protocolHeader::CommandType::LOGIN_REQUEST &&
        (responseCommand == protocolHeader::CommandType::LOGIN_SUCCESS ||
         responseCommand == protocolHeader::CommandType::LOGIN_FAILED);
    const bool isRegisterResponse =
        originalCommand == protocolHeader::CommandType::REGISTER_REQUEST &&
        (responseCommand == protocolHeader::CommandType::REGISTER_SUCCESS ||
         responseCommand == protocolHeader::CommandType::REGISTER_FAILED);
    const bool isLogoutResponse =
        originalCommand == protocolHeader::CommandType::LOGOUT_REQUEST &&
        (responseCommand == protocolHeader::CommandType::LOGOUT_SUCCESS ||
         responseCommand == protocolHeader::CommandType::LOGOUT_FAILED);

    m_authRequestPending = false;
    m_isLoggingOut = false;
    m_requestTimer.stop();

    if (!isLoginResponse && !isRegisterResponse && !isLogoutResponse)
    {
        // 如果登录/退出登录请求收到意外响应，断开连接以避免服务端和客户端的登录状态不一致。
        if (originalCommand == protocolHeader::CommandType::LOGIN_REQUEST ||
            originalCommand == protocolHeader::CommandType::LOGOUT_REQUEST)
        {
            m_isAuthenticated = false;
            m_currentAccount.clear();
            returnToLoginWindow();
            setNetworkStatus(QStringLiteral("服务器响应异常 · 请重新登录"), QStringLiteral("error"));
            if (m_tcpClient->isConnected())
            {
                m_tcpClient->abortConnection();
            }
        }
        m_pendingLoginAccount.clear();
        m_pendingRememberMe = false;
        showResponseMessage(
            QStringLiteral("服务器响应异常"),
            QStringLiteral("收到的响应命令与当前请求不匹配，请检查客户端与服务端协议版本。"),
            true);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        // 登录成功响应若无法解析，客户端不能确认当前身份；断开连接避免两端状态不一致。
        if (originalCommand == protocolHeader::CommandType::LOGIN_REQUEST ||
            originalCommand == protocolHeader::CommandType::LOGOUT_REQUEST)
        {
            m_isAuthenticated = false;
            m_currentAccount.clear();
            returnToLoginWindow();
            setNetworkStatus(QStringLiteral("服务器响应格式异常 · 请重新登录"), QStringLiteral("error"));
            if (m_tcpClient->isConnected())
            {
                m_tcpClient->abortConnection();
            }
        }
        m_pendingLoginAccount.clear();
        m_pendingRememberMe = false;
        showResponseMessage(
            QStringLiteral("服务器响应格式错误"),
            QStringLiteral("无法解析服务器返回的 JSON。"),
            true);
        return;
    }

    const QJsonObject body = document.object();
    const QString message = body.value(QString::fromLatin1(protocolBody::MESSAGE))
                                .toString(QStringLiteral("服务器已返回响应。"));

    switch (responseCommand)
    {
    case protocolHeader::CommandType::LOGIN_SUCCESS:
        // 只有服务器明确返回 LOGIN_SUCCESS，才设置本地登录状态并打开主窗口。
        m_isAuthenticated = true;
        m_currentAccount = m_pendingLoginAccount;
        m_pendingLoginAccount.clear();
        m_loginWindow->rememberSuccessfulLogin(m_currentAccount, m_pendingRememberMe);
        m_pendingRememberMe = false;
        m_loginWindow->notifyLoginSucceeded();
        break;

    case protocolHeader::CommandType::LOGIN_FAILED:
        m_pendingLoginAccount.clear();
        m_pendingRememberMe = false;
        setNetworkStatus(m_tcpClient->isConnected()
                             ? QStringLiteral("服务器已连接")
                             : QStringLiteral("连接已断开"),
                         m_tcpClient->isConnected()
                             ? QStringLiteral("connected")
                             : QStringLiteral("disconnected"));
        showResponseMessage(QStringLiteral("登录失败"), message, true);
        break;

    case protocolHeader::CommandType::REGISTER_SUCCESS:
        m_pendingRememberMe = false;
        setNetworkStatus(m_tcpClient->isConnected()
                             ? QStringLiteral("服务器已连接")
                             : QStringLiteral("连接已断开"),
                         m_tcpClient->isConnected()
                             ? QStringLiteral("connected")
                             : QStringLiteral("disconnected"));
        showResponseMessage(QStringLiteral("注册成功"), message, false);
        break;

    case protocolHeader::CommandType::REGISTER_FAILED:
        m_pendingRememberMe = false;
        setNetworkStatus(m_tcpClient->isConnected()
                             ? QStringLiteral("服务器已连接")
                             : QStringLiteral("连接已断开"),
                         m_tcpClient->isConnected()
                             ? QStringLiteral("connected")
                             : QStringLiteral("disconnected"));
        showResponseMessage(QStringLiteral("注册失败"), message, true);
        break;

    case protocolHeader::CommandType::LOGOUT_SUCCESS:
        // 只有服务端确认退出成功，才清除会话并显示登录窗口。
        m_isAuthenticated = false;
        m_currentAccount.clear();
        m_pendingLoginAccount.clear();
        m_pendingRememberMe = false;
        returnToLoginWindow();
        setNetworkStatus(QStringLiteral("服务器连接正常"), QStringLiteral("connected"));
        showResponseMessage(QStringLiteral("退出登录成功"), message, false);
        break;

    case protocolHeader::CommandType::LOGOUT_FAILED:
        // 服务器拒绝退出时，保留当前登录状态，用户仍可以在主界面继续操作。
        if (m_mainWindow != nullptr)
        {
            m_mainWindow->setMessageStatus(QString(), false);
        }
        showResponseMessage(QStringLiteral("退出登录失败"), message, true);
        break;

    default:
        break;
    }
}

void AppController::onMessageSendRequested(const QString &text,
                                           quint64 conversationId,
                                           bool isGroup)
{
    if (m_isLoggingOut)
    {
        if (m_mainWindow != nullptr)
        {
            m_mainWindow->setMessageStatus(
                QStringLiteral("正在退出登录，请稍候，暂时不能发送新消息"), true);
        }
        return;
    }

    if (m_mainWindow == nullptr || !m_isAuthenticated)
    {
        if (m_mainWindow != nullptr)
        {
            m_mainWindow->setMessageStatus(QStringLiteral("请先登录后再发送消息"), true);
        }
        return;
    }

    // 当前协议将已登录身份绑定在 TCP 连接上；连接失效后需要重新登录。
    if (!m_tcpClient->isConnected())
    {
        m_mainWindow->setMessageStatus(QStringLiteral("连接已断开，请重新登录；输入内容已保留"), true);
        return;
    }

    if (conversationId == 0)
    {
        m_mainWindow->setMessageStatus(
            QStringLiteral("请先选择一个真实会话，再发送消息"), true);
        return;
    }

    const QString content = text.trimmed();
    const QByteArray utf8Content = content.toUtf8();
    if (content.isEmpty())
    {
        return;
    }

    // 与服务端限制保持一致：最多 512 个 Unicode 字符，UTF-8 编码最多 2048 字节。
    if (content.toUcs4().size() > 512 || utf8Content.size() > 2048)
    {
        m_mainWindow->setMessageStatus(
            QStringLiteral("消息最多 512 个字符，且 UTF-8 编码不能超过 2048 字节"), true);
        return;
    }

    const QString clientMessageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject body;
    body.insert(QString::fromLatin1(protocolBody::CONTENT), content);
    body.insert(QString::fromLatin1(protocolBody::CONVERSATION_ID),
                QJsonValue(static_cast<qint64>(conversationId)));
    body.insert(QString::fromLatin1(protocolBody::CLIENT_MSG_ID), clientMessageId);
    body.insert(QString::fromLatin1(protocolBody::TYPE), 1); // 1 表示文本消息。

    const auto command = isGroup
        ? protocolHeader::CommandType::GROUP_MESSAGE_SEND
        : protocolHeader::CommandType::PRIVATE_MESSAGE_SEND;
    const quint64 requestId = m_nextRequestId++;
    const PendingRequest request{command, requestId, body};

    // 新的发送操作开始时清除上一条提示；本次若失败，会立即显示新的错误原因。
    m_mainWindow->setMessageStatus(QString(), false);

    // 若 TCP 层都没有接受请求，不创建气泡，也不清空编辑框。用户可直接再次点击发送。
    if (!sendRequest(request))
    {
        m_mainWindow->setMessageStatus(
            QStringLiteral("消息未能提交到网络层，请检查连接；输入内容已保留"), true);
        return;
    }

    // 请求已交给 TCP 层后，再插入本地待确认气泡并清空编辑框。
    ChatMessageRecord pendingMessage;
    pendingMessage.conversationId = conversationId;
    pendingMessage.clientMessageId = clientMessageId;
    pendingMessage.senderName = m_currentAccount;
    pendingMessage.content = content;
    pendingMessage.timeText = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
    pendingMessage.sendState = MessageSendState::Pending;
    m_mainWindow->appendMessage(pendingMessage);

    m_retryableMessages.insert(clientMessageId, RetryableMessage{command, body});
    m_messageRequestClientIds.insert(requestId, clientMessageId);
    m_messagesInFlight.insert(clientMessageId);
    startMessageRequestTimer(requestId, clientMessageId);

    m_mainWindow->clearMessageInput();
    m_mainWindow->setMessageStatus(QStringLiteral("消息已提交，等待服务器确认…"), false);
}

void AppController::onMessageRetryRequested(const QString &clientMessageId)
{
    if (clientMessageId.isEmpty() || !m_retryableMessages.contains(clientMessageId))
    {
        return;
    }
    if (m_messagesInFlight.contains(clientMessageId))
    {
        // 一条消息在任何时刻都只能有一次发送请求在等待确认。
        return;
    }
    if (!m_isAuthenticated || m_isLoggingOut || m_mainWindow == nullptr ||
        !m_tcpClient->isConnected())
    {
        if (m_mainWindow != nullptr)
        {
            m_mainWindow->setMessageStatus(
                QStringLiteral("连接已失效，请重新登录后再重试消息"), true);
        }
        return;
    }

    // 使用保存的原请求正文；特别是 client_msg_id 绝不重新生成，确保服务端幂等。
    const RetryableMessage retry = m_retryableMessages.value(clientMessageId);
    const quint64 requestId = m_nextRequestId++;
    const PendingRequest request{retry.command, requestId, retry.body};
    m_mainWindow->setMessageStatus(QString(), false);
    if (!sendRequest(request))
    {
        m_mainWindow->setMessageSendState(clientMessageId, MessageSendState::Failed);
        m_mainWindow->setMessageStatus(
            QStringLiteral("重试请求未能提交到网络层，请检查连接"), true);
        return;
    }

    m_mainWindow->setMessageSendState(clientMessageId, MessageSendState::Pending);
    m_messageRequestClientIds.insert(requestId, clientMessageId);
    m_messagesInFlight.insert(clientMessageId);
    startMessageRequestTimer(requestId, clientMessageId);
    m_mainWindow->setMessageStatus(QStringLiteral("正在重试发送消息…"), false);
}

void AppController::onLogoutRequested()
{
    if (!m_isAuthenticated)
    {
        showResponseMessage(QStringLiteral("当前未登录"),
                            QStringLiteral("当前没有已登录的账号。"), false);
        return;
    }

    if (m_isLoggingOut)
    {
        return;
    }

    // 退出登录依赖当前 TCP 连接中的服务端会话身份。若连接已断开，就不能只发送一个新连接请求。
    if (!m_tcpClient->isConnected())
    {
        m_isAuthenticated = false;
        m_currentAccount.clear();
        returnToLoginWindow();
        showResponseMessage(
            QStringLiteral("连接已断开"),
            QStringLiteral("当前连接已失效，客户端已返回登录界面，请重新登录。"),
            true);
        return;
    }

    m_isLoggingOut = true;
    m_requestTimer.start(15000);
    if (m_mainWindow != nullptr)
    {
        m_mainWindow->setMessageStatus(QStringLiteral("正在退出登录…"), false);
    }

    // 服务端从当前 TCP 连接绑定的 ClientSession 识别用户，因此退出请求无需携带密码或账号。
    const PendingRequest request{
        protocolHeader::CommandType::LOGOUT_REQUEST,
        m_nextRequestId++,
        QJsonObject{}};
    sendRequest(request);
}

void AppController::handleIncomingMessagePush(protocolHeader::CommandType command,
                                                   quint64 requestId,
                                                   const QByteArray &jsonBody)
{
    // 服务端约定主动推送的 request_id 必须为 0。其他值意味着协议行为异常，
    // 不把这个数据包混入普通请求响应流程。
    if (requestId != 0)
    {
        qWarning() << "忽略 request_id 非 0 的消息推送:" << requestId;
        return;
    }

    // 只有处于服务器确认的登录状态时，才接受聊天推送。
    if (!m_isAuthenticated || m_isLoggingOut || m_mainWindow == nullptr)
    {
        qWarning() << "当前没有已认证的主窗口，忽略聊天消息推送";
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        m_mainWindow->setMessageStatus(QStringLiteral("收到无法解析的消息推送"), true);
        qWarning() << "聊天推送 JSON 格式错误:" << parseError.errorString();
        return;
    }

    const QJsonObject body = document.object();
    ChatMessageRecord message;
    if (!readPositiveJsonInteger(body, protocolBody::CONVERSATION_ID, message.conversationId) ||
        !readPositiveJsonInteger(body, protocolBody::SERVER_MSG_ID, message.serverMessageId) ||
        !readPositiveJsonInteger(body, protocolBody::USER_ID, message.senderId) ||
        !readPositiveJsonInteger(body, protocolBody::SEQ, message.sequence))
    {
        m_mainWindow->setMessageStatus(QStringLiteral("消息推送缺少有效的会话或消息标识"), true);
        qWarning() << "聊天推送 ID/seq 字段不合法";
        return;
    }

    const QJsonValue usernameValue = body.value(QString::fromLatin1(protocolBody::USERNAME));
    const QJsonValue contentValue = body.value(QString::fromLatin1(protocolBody::CONTENT));
    const QJsonValue clientMessageIdValue =
        body.value(QString::fromLatin1(protocolBody::CLIENT_MSG_ID));
    const QJsonValue typeValue = body.value(QString::fromLatin1(protocolBody::TYPE));

    if (!usernameValue.isString() || !contentValue.isString() ||
        !clientMessageIdValue.isString() || !typeValue.isDouble())
    {
        m_mainWindow->setMessageStatus(QStringLiteral("消息推送的数据字段不完整"), true);
        qWarning() << "聊天推送 username/content/client_msg_id/type 字段格式错误";
        return;
    }

    const QString content = contentValue.toString();
    const QString senderName = usernameValue.toString();
    const QString clientMessageId = clientMessageIdValue.toString();
    const int messageType = typeValue.toInt(-1);
    if (messageType != 1 || content.isEmpty() ||
        content.toUcs4().size() > 512 || content.toUtf8().size() > 2048 ||
        senderName.trimmed().isEmpty() || clientMessageId.isEmpty() || clientMessageId.size() > 64)
    {
        // 当前项目只做文本消息；尚未实现的图片、文件等消息类型不会被误画成文本。
        qWarning() << "忽略不支持或不合法的消息类型/内容/发送者标识:" << messageType;
        return;
    }

    message.senderName = senderName;
    message.clientMessageId = clientMessageId;
    message.content = content;
    message.timeText = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));

    // 消息模型按 conversation_id 隔离视图，并按 seq 排序；server_msg_id / client_msg_id 去重。
    // 即使当前没有打开这条会话，消息仍保留在客户端模型中，切回会话后即可显示。
    if (!m_mainWindow->appendMessage(message))
    {
        // 重复推送是正常的网络边界情况，静默忽略，不弹打扰用户的提示。
        qDebug() << "忽略重复或无效的聊天消息:" << message.serverMessageId;
    }
    else
    {
        // 新消息到达后更新会话列表里的摘要/排序；请求在途时只安排一次后续刷新。
        requestConversationList();
    }

    // 有些服务端路径会把发送者自己的已保存消息也推回来。该推送同样可以
    // 确认乐观消息，并覆盖先前可能出现的发送超时状态。
    if (message.senderName.compare(m_currentAccount, Qt::CaseInsensitive) == 0)
    {
        m_mainWindow->setMessageSendState(message.clientMessageId, MessageSendState::Sent);
        m_retryableMessages.remove(message.clientMessageId);
    }

    Q_UNUSED(command); // 私聊/群聊命令只用来区分协议事件，气泡样式不依赖会话类型。
}

quint64 AppController::sendOperationRequest(protocolHeader::CommandType command,
                                           const QJsonObject &body)
{
    if (!m_isAuthenticated || m_isLoggingOut || m_mainWindow == nullptr)
    {
        if (m_mainWindow != nullptr)
        {
            m_mainWindow->setMessageStatus(QStringLiteral("请先登录后再执行此操作"), true);
        }
        return 0;
    }
    if (!m_tcpClient->isConnected())
    {
        m_mainWindow->setMessageStatus(QStringLiteral("连接已断开，请重新登录后重试"), true);
        return 0;
    }

    const quint64 requestId = m_nextRequestId++;
    const PendingRequest request{command, requestId, body};
    if (!sendRequest(request))
    {
        m_mainWindow->setMessageStatus(QStringLiteral("请求发送失败，请检查网络连接"), true);
        return 0;
    }

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    m_operationRequestTimers.insert(requestId, timer);
    connect(timer, &QTimer::timeout, this, [this, requestId, command]()
    {
        // 如果响应已处理或连接清理已发生，此计时器不会再进入这里；仍做一次防御检查。
        if (m_requestsAwaitingResponse.remove(requestId) == 0)
        {
            stopOperationRequestTimer(requestId);
            return;
        }

        stopOperationRequestTimer(requestId);
        const bool friendListWasForNewSession =
            command == protocolHeader::CommandType::QUERY_FRIEND_LIST_REQUEST &&
            m_friendListRequestForNewSession;
        switch (command)
        {
        case protocolHeader::CommandType::QUERY_CONVERSATION_LIST_REQUEST:
            if (m_conversationListRequestId == requestId)
            {
                m_conversationListRequestId = 0;
            }
            m_conversationListRefreshPending = false;
            m_pendingConversationSelectionId = 0;
            break;
        case protocolHeader::CommandType::QUERY_FRIEND_LIST_REQUEST:
            if (m_friendListRequestId == requestId)
            {
                m_friendListRequestId = 0;
            }
            m_friendListRequestForNewSession = false;
            break;
        case protocolHeader::CommandType::QUERY_PENDING_FRIEND_LIST_REQUEST:
            if (m_pendingFriendListRequestId == requestId)
            {
                m_pendingFriendListRequestId = 0;
            }
            break;
        case protocolHeader::CommandType::FRIEND_REQUEST_SEND:
            if (m_sendFriendRequestId == requestId)
            {
                m_sendFriendRequestId = 0;
            }
            break;
        case protocolHeader::CommandType::FRIEND_REQUEST_HANDLE_REQUEST:
            if (m_handleFriendRequestId == requestId)
            {
                m_handleFriendRequestId = 0;
                m_pendingHandledFriendUserId = 0;
                m_pendingFriendRequestAction = 0;
            }
            break;
        case protocolHeader::CommandType::CHATCREATE_REQUEST:
            if (m_createConversationRequestId == requestId)
            {
                m_createConversationRequestId = 0;
            }
            break;
        default:
            break;
        }

        if (m_mainWindow != nullptr)
        {
            QString status = QStringLiteral("服务器请求超时，请稍后重试");
            if (command == protocolHeader::CommandType::QUERY_CONVERSATION_LIST_REQUEST)
            {
                status = QStringLiteral("会话列表加载超时，请重试");
            }
            else if (command == protocolHeader::CommandType::QUERY_FRIEND_LIST_REQUEST)
            {
                status = QStringLiteral("好友列表加载超时，请重试");
                m_mainWindow->setContactsStatus(status, true);
                if (friendListWasForNewSession)
                {
                    m_mainWindow->setMessageStatus(status, true);
                }
                m_friendListRequestForNewSession = false;
            }
            else if (command == protocolHeader::CommandType::QUERY_PENDING_FRIEND_LIST_REQUEST)
            {
                status = QStringLiteral("收到的好友申请加载超时，请重试");
                m_mainWindow->setContactsStatus(status, true);
            }
            else if (command == protocolHeader::CommandType::FRIEND_REQUEST_SEND)
            {
                status = QStringLiteral("发送好友申请超时，请刷新后确认状态");
                m_mainWindow->finishAddFriendRequest(false);
                m_mainWindow->setContactsStatus(status, true);
            }
            else if (command == protocolHeader::CommandType::FRIEND_REQUEST_HANDLE_REQUEST)
            {
                status = QStringLiteral("处理好友申请超时，请刷新列表确认状态");
                m_mainWindow->setContactsStatus(status, true);
            }
            else if (command == protocolHeader::CommandType::CHATCREATE_REQUEST)
            {
                status = QStringLiteral("创建/打开会话超时，请检查连接后重试");
            }
            m_mainWindow->setMessageStatus(status, true);
        }
    });
    timer->start(15000);
    return requestId;
}

void AppController::stopOperationRequestTimer(quint64 requestId)
{
    QTimer *timer = m_operationRequestTimers.take(requestId);
    if (timer != nullptr)
    {
        timer->stop();
        timer->deleteLater();
    }
}

void AppController::clearOperationRequestTimers()
{
    const auto timers = m_operationRequestTimers;
    m_operationRequestTimers.clear();
    for (QTimer *timer : timers)
    {
        if (timer != nullptr)
        {
            timer->stop();
            timer->deleteLater();
        }
    }
    m_conversationListRequestId = 0;
    m_friendListRequestId = 0;
    m_pendingFriendListRequestId = 0;
    m_sendFriendRequestId = 0;
    m_handleFriendRequestId = 0;
    m_friendRequestSenderIds.clear();
    m_pendingHandledFriendUserId = 0;
    m_pendingFriendRequestAction = 0;
    m_createConversationRequestId = 0;
    m_pendingConversationSelectionId = 0;
    m_conversationListRefreshPending = false;
    m_friendListRequestForNewSession = false;
}

void AppController::requestConversationList()
{
    if (!m_isAuthenticated || m_isLoggingOut || m_mainWindow == nullptr)
    {
        return;
    }
    if (!m_tcpClient->isConnected())
    {
        m_mainWindow->setMessageStatus(QStringLiteral("连接已断开，无法刷新会话列表"), true);
        return;
    }
    if (m_conversationListRequestId != 0)
    {
        // 一次只允许一个会话列表查询在途。连续收到消息时合并刷新，避免请求风暴。
        m_conversationListRefreshPending = true;
        return;
    }

    const quint64 requestId = sendOperationRequest(
        protocolHeader::CommandType::QUERY_CONVERSATION_LIST_REQUEST, QJsonObject{});
    if (requestId != 0)
    {
        m_conversationListRequestId = requestId;
    }
}

void AppController::handleConversationListResponse(
    protocolHeader::CommandType responseCommand,
    const QByteArray &jsonBody)
{
    if (m_mainWindow == nullptr)
    {
        return;
    }
    if (responseCommand != protocolHeader::CommandType::QUERY_CONVERSATION_LIST_SUCCESS &&
        responseCommand != protocolHeader::CommandType::QUERY_CONVERSATION_LIST_FAILED)
    {
        m_mainWindow->setMessageStatus(QStringLiteral("服务器返回了无法识别的会话列表响应"), true);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        m_mainWindow->setMessageStatus(QStringLiteral("无法解析服务器返回的会话列表"), true);
        return;
    }

    const QJsonObject body = document.object();
    const QString serverMessage = body.value(QString::fromLatin1(protocolBody::MESSAGE))
                                      .toString(QStringLiteral("会话列表请求已完成"));
    if (responseCommand == protocolHeader::CommandType::QUERY_CONVERSATION_LIST_FAILED)
    {
        m_mainWindow->setMessageStatus(serverMessage, true);
        return;
    }

    const QJsonValue dataValue = body.value(QString::fromLatin1(protocolBody::DATA));
    if (!dataValue.isArray())
    {
        m_mainWindow->setMessageStatus(QStringLiteral("会话列表响应缺少 data 数组"), true);
        return;
    }

    QVector<SessionItem> sessions;
    const QJsonArray items = dataValue.toArray();
    for (const QJsonValue &itemValue : items)
    {
        if (!itemValue.isObject())
        {
            qWarning() << "跳过非对象的会话列表项目";
            continue;
        }
        const QJsonObject item = itemValue.toObject();
        quint64 conversationId = 0;
        if (!readPositiveJsonInteger(item, protocolBody::CONVERSATION_ID, conversationId))
        {
            qWarning() << "跳过缺少有效 conversation_id 的会话";
            continue;
        }

        const QJsonValue typeValue = item.value(QString::fromLatin1(protocolBody::TYPE));
        if (!typeValue.isDouble())
        {
            qWarning() << "跳过缺少会话 type 的会话" << conversationId;
            continue;
        }
        const qint64 type = typeValue.toInteger(-1);
        if (type != 1 && type != 2)
        {
            qWarning() << "跳过不支持的会话 type" << type;
            continue;
        }

        QString displayName = item.value(QString::fromLatin1(protocolBody::DISPLAY_NAME)).toString().trimmed();
        const QString username = item.value(QString::fromLatin1(protocolBody::USERNAME)).toString().trimmed();
        if (displayName.isEmpty())
        {
            displayName = !username.isEmpty()
                              ? username
                              : (type == 2 ? QStringLiteral("未命名群聊") : QStringLiteral("未知联系人"));
        }

        SessionItem session;
        session.id = QString::number(conversationId);
        session.name = displayName;
        session.time = displayConversationTime(
            item.value(QString::fromLatin1(protocolBody::LAST_TIME)).toString());
        session.preview = item.value(QString::fromLatin1(protocolBody::LAST_MESSAGE)).toString();
        if (session.preview.isEmpty())
        {
            session.preview = QStringLiteral("暂无消息");
        }
        session.avatarText = displayName.left(1);
        session.group = type == 2;
        session.unread = false;
        session.unreadCount = 0; // 服务端当前没有返回未读数，不显示虚构未读气泡。
        session.conversationId = conversationId;

        // 头像暂未使用图片 URL，按会话类型/顺序分配主题渐变色。
        if (session.group)
        {
            session.avatarStart = Theme::MintStart;
            session.avatarEnd = Theme::MintEnd;
        }
        else
        {
            switch (sessions.size() % 3)
            {
            case 0:
                session.avatarStart = Theme::PeachStart;
                session.avatarEnd = Theme::PeachEnd;
                break;
            case 1:
                session.avatarStart = Theme::BlueStart;
                session.avatarEnd = Theme::BlueEnd;
                break;
            default:
                session.avatarStart = Theme::GoldStart;
                session.avatarEnd = Theme::GoldEnd;
                break;
            }
        }
        sessions.push_back(session);
    }

    // 非空的服务器列表若所有条目都无效，应提示协议/数据问题，而不是伪装成空列表。
    if (!items.isEmpty() && sessions.isEmpty())
    {
        m_mainWindow->setMessageStatus(QStringLiteral("服务器返回的会话列表没有有效条目"), true);
        return;
    }

    quint64 preferredId = 0;
    if (m_pendingConversationSelectionId != 0)
    {
        for (const SessionItem &session : sessions)
        {
            if (session.conversationId == m_pendingConversationSelectionId)
            {
                preferredId = m_pendingConversationSelectionId;
                m_pendingConversationSelectionId = 0;
                break;
            }
        }
    }
    m_mainWindow->setSessions(sessions, preferredId);
    if (sessions.isEmpty())
    {
        m_mainWindow->setMessageStatus(
            QStringLiteral("当前没有会话，点击会话栏的 + 选择好友开始私聊"), false);
    }
    else if (preferredId != 0)
    {
        m_mainWindow->setMessageStatus(QStringLiteral("已打开会话"), false);
    }
}

void AppController::onNewSessionRequested()
{
    if (!m_isAuthenticated || m_isLoggingOut || m_mainWindow == nullptr)
    {
        return;
    }
    if (m_friendListRequestId != 0 || m_createConversationRequestId != 0)
    {
        m_mainWindow->setMessageStatus(QStringLiteral("正在处理新建会话请求，请稍候"), false);
        return;
    }
    if (!m_tcpClient->isConnected())
    {
        m_mainWindow->setMessageStatus(QStringLiteral("连接已断开，请重新登录后再新建会话"), true);
        return;
    }

    m_friendListRequestForNewSession = true;
    const quint64 requestId = sendOperationRequest(
        protocolHeader::CommandType::QUERY_FRIEND_LIST_REQUEST, QJsonObject{});
    if (requestId != 0)
    {
        m_friendListRequestId = requestId;
        m_mainWindow->setMessageStatus(QStringLiteral("正在加载好友列表…"), false);
    }
    else
    {
        m_friendListRequestForNewSession = false;
    }
}

void AppController::onContactsRefreshRequested()
{
    if (!m_isAuthenticated || m_isLoggingOut || m_mainWindow == nullptr)
    {
        return;
    }
    if (!m_tcpClient->isConnected())
    {
        m_mainWindow->setContactsStatus(QStringLiteral("连接已断开，请重新登录后刷新联系人。"), true);
        return;
    }

    m_mainWindow->setContactsStatus(QStringLiteral("正在从服务器刷新好友和收到的申请…"), false);
    if (m_friendListRequestId != 0)
    {
        m_friendListRequestForNewSession = false;
    }
    requestFriendListForContacts();
    requestPendingFriendRequests();
}

void AppController::requestFriendListForContacts()
{
    if (m_friendListRequestId != 0 || !m_isAuthenticated || m_mainWindow == nullptr)
    {
        return;
    }

    m_friendListRequestForNewSession = false;
    const quint64 requestId = sendOperationRequest(
        protocolHeader::CommandType::QUERY_FRIEND_LIST_REQUEST, QJsonObject{});
    if (requestId != 0)
    {
        m_friendListRequestId = requestId;
    }
    else
    {
        m_mainWindow->setContactsStatus(
            QStringLiteral("无法发送好友列表查询请求，请检查连接后重试。"), true);
    }
}

void AppController::requestPendingFriendRequests()
{
    if (m_pendingFriendListRequestId != 0 || !m_isAuthenticated || m_mainWindow == nullptr)
    {
        return;
    }

    const quint64 requestId = sendOperationRequest(
        protocolHeader::CommandType::QUERY_PENDING_FRIEND_LIST_REQUEST, QJsonObject{});
    if (requestId != 0)
    {
        m_pendingFriendListRequestId = requestId;
    }
    else
    {
        m_mainWindow->setContactsStatus(
            QStringLiteral("无法发送好友申请列表查询请求，请检查连接后重试。"), true);
    }
}

void AppController::onFriendRequestSendRequested(const QString &username)
{
    if (!m_isAuthenticated || m_isLoggingOut || m_mainWindow == nullptr)
    {
        if (m_mainWindow != nullptr)
        {
            m_mainWindow->finishAddFriendRequest(false);
            m_mainWindow->setContactsStatus(QStringLiteral("请先登录后再添加好友。"), true);
        }
        return;
    }
    if (m_sendFriendRequestId != 0)
    {
        m_mainWindow->finishAddFriendRequest(false);
        m_mainWindow->setContactsStatus(QStringLiteral("上一条好友申请仍在处理中，请稍候。"), false);
        return;
    }

    QJsonObject body;
    body.insert(QString::fromLatin1(protocolBody::TARGET_USERNAME), username.trimmed());
    const quint64 requestId = sendOperationRequest(
        protocolHeader::CommandType::FRIEND_REQUEST_SEND, body);
    if (requestId != 0)
    {
        m_sendFriendRequestId = requestId;
        m_mainWindow->setContactsStatus(QStringLiteral("正在发送好友申请…"), false);
    }
    else
    {
        m_mainWindow->finishAddFriendRequest(false);
        m_mainWindow->setContactsStatus(
            QStringLiteral("好友申请未发送，请检查连接后重试。"), true);
    }
}

void AppController::onFriendRequestHandleRequested(quint64 requestId,
                                                   int action)
{
    if (!m_isAuthenticated || m_isLoggingOut || m_mainWindow == nullptr)
    {
        return;
    }
    // 服务端只需要申请记录 ID 和操作类型。申请人 user_id 是客户端后续自动打开私聊时
    // 使用的辅助信息，不应该成为同意/拒绝请求是否发送的前置条件。
    if (requestId == 0 || (action != 1 && action != 2))
    {
        m_mainWindow->setContactsStatus(QStringLiteral("好友申请记录或操作类型无效，未发送请求。"), true);
        return;
    }
    const quint64 senderUserId = m_friendRequestSenderIds.value(requestId, 0);
    if (m_handleFriendRequestId != 0)
    {
        m_mainWindow->setContactsStatus(QStringLiteral("正在处理另一条好友申请，请稍候。"), false);
        return;
    }

    QJsonObject body;
    body.insert(QString::fromLatin1(protocolBody::FRIEND_REQUEST_ID),
                QJsonValue(static_cast<qint64>(requestId)));
    body.insert(QString::fromLatin1(protocolBody::ACTION), action);
    const quint64 networkRequestId = sendOperationRequest(
        protocolHeader::CommandType::FRIEND_REQUEST_HANDLE_REQUEST, body);
    if (networkRequestId != 0)
    {
        m_handleFriendRequestId = networkRequestId;
        m_pendingHandledFriendUserId = senderUserId;
        m_pendingFriendRequestAction = action;
        m_mainWindow->setContactsStatus(
            action == 1 ? QStringLiteral("正在同意好友申请…") : QStringLiteral("正在拒绝好友申请…"), false);
    }
    else
    {
        m_mainWindow->setContactsStatus(
            QStringLiteral("未能发送好友申请处理请求，请检查连接后重试。"), true);
    }
}

void AppController::onContactOpenPrivateChatRequested(quint64 userId)
{
    openPrivateConversation(userId);
}

void AppController::openPrivateConversation(quint64 targetUserId)
{
    if (!m_isAuthenticated || m_isLoggingOut || m_mainWindow == nullptr)
    {
        return;
    }
    if (targetUserId == 0)
    {
        m_mainWindow->setContactsStatus(QStringLiteral("所选好友 ID 无效，无法创建会话。"), true);
        return;
    }
    if (m_createConversationRequestId != 0)
    {
        m_mainWindow->setContactsStatus(QStringLiteral("正在打开另一条会话，请稍候。"), false);
        return;
    }

    QJsonObject body;
    body.insert(QString::fromLatin1(protocolBody::TYPE), 1); // 1=私聊。
    body.insert(QString::fromLatin1(protocolBody::TARGET_USER_ID),
                QJsonValue(static_cast<qint64>(targetUserId)));
    const quint64 requestId = sendOperationRequest(
        protocolHeader::CommandType::CHATCREATE_REQUEST, body);
    if (requestId != 0)
    {
        m_createConversationRequestId = requestId;
        m_mainWindow->setContactsStatus(QStringLiteral("正在打开私聊会话…"), false);
        m_mainWindow->setMessageStatus(QStringLiteral("正在打开私聊会话…"), false);
    }
}

void AppController::handleFriendListResponse(
    protocolHeader::CommandType responseCommand,
    const QByteArray &jsonBody)
{
    if (m_mainWindow == nullptr)
    {
        return;
    }
    const bool forNewSession = m_friendListRequestForNewSession;
    m_friendListRequestForNewSession = false;

    if (responseCommand != protocolHeader::CommandType::QUERY_FRIEND_LIST_SUCCESS &&
        responseCommand != protocolHeader::CommandType::QUERY_FRIEND_LIST_FAILED)
    {
        const QString message = QStringLiteral("服务器返回了无法识别的好友列表响应");
        m_mainWindow->setContactsStatus(message, true);
        if (forNewSession)
        {
            m_mainWindow->setMessageStatus(message, true);
        }
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        const QString message = QStringLiteral("无法解析服务器返回的好友列表");
        m_mainWindow->setContactsStatus(message, true);
        if (forNewSession)
        {
            m_mainWindow->setMessageStatus(message, true);
        }
        return;
    }

    const QJsonObject body = document.object();
    const QString serverMessage = body.value(QString::fromLatin1(protocolBody::MESSAGE))
                                      .toString(QStringLiteral("好友列表请求已完成"));
    if (responseCommand == protocolHeader::CommandType::QUERY_FRIEND_LIST_FAILED)
    {
        m_mainWindow->setContactsStatus(serverMessage, true);
        if (forNewSession)
        {
            m_mainWindow->setMessageStatus(serverMessage, true);
        }
        return;
    }

    const QJsonValue dataValue = body.value(QString::fromLatin1(protocolBody::DATA));
    if (!dataValue.isArray())
    {
        const QString message = QStringLiteral("好友列表响应缺少 data 数组");
        m_mainWindow->setContactsStatus(message, true);
        if (forNewSession)
        {
            m_mainWindow->setMessageStatus(message, true);
        }
        return;
    }

    QVector<ContactItem> contacts;
    const QJsonArray items = dataValue.toArray();
    for (const QJsonValue &value : items)
    {
        if (!value.isObject())
        {
            continue;
        }
        const QJsonObject item = value.toObject();
        ContactItem contact;
        if (!readPositiveJsonInteger(item, protocolBody::USER_ID, contact.userId))
        {
            continue;
        }
        contact.username = item.value(QString::fromLatin1(protocolBody::USERNAME)).toString().trimmed();
        contact.nickname = item.value(QString::fromLatin1(protocolBody::NICKNAME)).toString().trimmed();
        contact.avatar = item.value(QString::fromLatin1(protocolBody::AVATAR)).toString();
        contact.createdAt = item.value(QString::fromLatin1(protocolBody::CREATED_AT)).toString();
        if (contact.username.isEmpty())
        {
            continue;
        }
        contacts.push_back(contact);
    }

    m_mainWindow->setContacts(contacts);
    m_mainWindow->setContactsStatus(QStringLiteral("好友列表已更新，共 %1 位好友。").arg(contacts.size()), false);
    if (forNewSession)
    {
        promptFriendSelection(dataValue.toArray());
    }
}

void AppController::handlePendingFriendListResponse(
    protocolHeader::CommandType responseCommand,
    const QByteArray &jsonBody)
{
    if (m_mainWindow == nullptr)
    {
        return;
    }
    if (responseCommand != protocolHeader::CommandType::QUERY_PENDING_FRIEND_LIST_SUCCESS &&
        responseCommand != protocolHeader::CommandType::QUERY_PENDING_FRIEND_LIST_FAILED)
    {
        m_mainWindow->setContactsStatus(QStringLiteral("服务器返回了无法识别的好友申请列表响应"), true);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        m_mainWindow->setContactsStatus(QStringLiteral("无法解析服务器返回的好友申请列表"), true);
        return;
    }

    const QJsonObject body = document.object();
    const QString serverMessage = body.value(QString::fromLatin1(protocolBody::MESSAGE))
                                      .toString(QStringLiteral("收到的好友申请查询已完成"));
    if (responseCommand == protocolHeader::CommandType::QUERY_PENDING_FRIEND_LIST_FAILED)
    {
        m_mainWindow->setContactsStatus(serverMessage, true);
        return;
    }

    const QJsonValue dataValue = body.value(QString::fromLatin1(protocolBody::DATA));
    if (!dataValue.isArray())
    {
        m_mainWindow->setContactsStatus(QStringLiteral("好友申请列表响应缺少 data 数组"), true);
        return;
    }

    QVector<PendingFriendRequestItem> requests;
    QHash<quint64, quint64> requestSenderIds;
    for (const QJsonValue &value : dataValue.toArray())
    {
        if (!value.isObject())
        {
            continue;
        }
        const QJsonObject item = value.toObject();
        PendingFriendRequestItem request;
        // 申请记录 ID 是同意/拒绝所必需的字段；申请人 ID 只用于之后自动打开私聊。
        if (!readPositiveJsonInteger(item, protocolBody::FRIEND_REQUEST_ID, request.requestId))
        {
            continue;
        }
        readPositiveJsonInteger(item, protocolBody::USER_ID, request.senderId);
        request.username = item.value(QString::fromLatin1(protocolBody::USERNAME)).toString().trimmed();
        request.nickname = item.value(QString::fromLatin1(protocolBody::NICKNAME)).toString().trimmed();
        request.avatar = item.value(QString::fromLatin1(protocolBody::AVATAR)).toString();
        request.createdAt = item.value(QString::fromLatin1(protocolBody::CREATED_AT)).toString();
        if (!request.username.isEmpty())
        {
            requestSenderIds.insert(request.requestId, request.senderId);
            requests.push_back(request);
        }
    }

    // 原子替换本次服务器返回的申请人与申请记录映射，避免 UI 行数据和控制器状态错位。
    m_friendRequestSenderIds = requestSenderIds;
    m_mainWindow->setPendingFriendRequests(requests);
    m_mainWindow->setContactsStatus(QStringLiteral("收到的好友申请已更新，共 %1 条待处理申请。").arg(requests.size()), false);
}

void AppController::handleFriendRequestSendResponse(
    protocolHeader::CommandType responseCommand,
    const QByteArray &jsonBody)
{
    if (m_mainWindow == nullptr)
    {
        return;
    }
    if (responseCommand != protocolHeader::CommandType::FRIEND_REQUEST_SEND_SUCCESS &&
        responseCommand != protocolHeader::CommandType::FRIEND_REQUEST_SEND_FAILED)
    {
        m_mainWindow->finishAddFriendRequest(false);
        m_mainWindow->setContactsStatus(QStringLiteral("服务器返回了无法识别的好友申请响应"), true);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        m_mainWindow->finishAddFriendRequest(false);
        m_mainWindow->setContactsStatus(QStringLiteral("无法解析服务器返回的好友申请结果"), true);
        return;
    }

    const QJsonObject body = document.object();
    const QString message = body.value(QString::fromLatin1(protocolBody::MESSAGE))
                                .toString(QStringLiteral("好友申请请求已完成"));
    const bool success = responseCommand == protocolHeader::CommandType::FRIEND_REQUEST_SEND_SUCCESS;
    m_mainWindow->finishAddFriendRequest(success);
    m_mainWindow->setContactsStatus(message, !success);
}

void AppController::handleFriendRequestHandleResponse(
    protocolHeader::CommandType responseCommand,
    const QByteArray &jsonBody)
{
    // 只有一条好友申请处理请求允许在途，因此在收到结果时取出对应的申请方 ID 和操作。
    const quint64 handledFriendUserId = m_pendingHandledFriendUserId;
    const int handledAction = m_pendingFriendRequestAction;
    m_pendingHandledFriendUserId = 0;
    m_pendingFriendRequestAction = 0;

    if (m_mainWindow == nullptr)
    {
        return;
    }
    if (responseCommand != protocolHeader::CommandType::FRIEND_REQUEST_HANDLE_SUCCESS &&
        responseCommand != protocolHeader::CommandType::FRIEND_REQUEST_HANDLE_FAILED)
    {
        m_mainWindow->setContactsStatus(QStringLiteral("服务器返回了无法识别的好友申请处理结果"), true);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        m_mainWindow->setContactsStatus(QStringLiteral("无法解析服务器返回的好友申请处理结果"), true);
        return;
    }

    const QJsonObject body = document.object();
    const QString message = body.value(QString::fromLatin1(protocolBody::MESSAGE))
                                .toString(QStringLiteral("好友申请处理完成"));
    const bool success = responseCommand == protocolHeader::CommandType::FRIEND_REQUEST_HANDLE_SUCCESS;
    m_mainWindow->setContactsStatus(message, !success);
    if (success)
    {
        // 同意会新增好友关系；同意或拒绝都会从待处理列表中移除该申请。
        requestFriendListForContacts();
        requestPendingFriendRequests();

        if (handledAction == 1 && handledFriendUserId > 0)
        {
            // 服务端只向已打开/订阅会话的连接推送消息。
            // 同意好友后立即创建或复用双方唯一的私聊，并订阅该会话，避免对方首条消息被跳过。
            m_mainWindow->showMessagesPage();
            openPrivateConversation(handledFriendUserId);
        }
    }
}

void AppController::promptFriendSelection(const QJsonArray &friends)
{
    if (m_mainWindow == nullptr)
    {
        return;
    }

    struct FriendChoice
    {
        quint64 userId;
        QString display;
    };
    QVector<FriendChoice> choices;
    for (const QJsonValue &value : friends)
    {
        if (!value.isObject())
        {
            continue;
        }
        const QJsonObject object = value.toObject();
        quint64 userId = 0;
        if (!readPositiveJsonInteger(object, protocolBody::USER_ID, userId))
        {
            continue;
        }
        const QString username = object.value(QString::fromLatin1(protocolBody::USERNAME)).toString().trimmed();
        const QString nickname = object.value(QString::fromLatin1(protocolBody::NICKNAME)).toString().trimmed();
        const QString name = nickname.isEmpty() ? username : nickname;
        if (name.isEmpty())
        {
            continue;
        }
        choices.push_back({userId, username.isEmpty() || username == name
                                       ? name
                                       : QStringLiteral("%1  @%2").arg(name, username)});
    }

    if (choices.isEmpty())
    {
        m_mainWindow->setMessageStatus(
            QStringLiteral("好友列表为空或没有有效好友，请先添加好友后再开始私聊"), false);
        return;
    }

    // 使用无系统标题栏的紫灰卡片，和主界面及 ModernMessageBox 保持一致。
    QDialog dialog(m_mainWindow);
    dialog.setWindowTitle(QStringLiteral("新建私聊"));
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground, true);
    dialog.setModal(true);
    dialog.setFixedWidth(460);

    auto *outerLayout = new QVBoxLayout(&dialog);
    outerLayout->setContentsMargins(18, 18, 18, 18);
    outerLayout->setSpacing(0);

    auto *card = new QFrame(&dialog);
    card->setObjectName(QStringLiteral("privateChatDialogCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setStyleSheet(QStringLiteral(
        "QFrame#privateChatDialogCard { background: %1; border: 1px solid #E8E1F5; border-radius: 18px; }")
        .arg(Theme::Surface.name()));

    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(30.0);
    shadow->setOffset(0.0, 8.0);
    shadow->setColor(Theme::WindowShadowColor);
    card->setGraphicsEffect(shadow);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(0);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(10);
    auto *titleColumn = new QVBoxLayout;
    titleColumn->setSpacing(5);
    auto *titleLabel = new QLabel(QStringLiteral("新建私聊"), card);
    titleLabel->setStyleSheet(QStringLiteral(
        "color: %1; background: transparent; border: none; font-size: 17px; font-weight: 750;")
        .arg(Theme::Ink.name()));
    auto *subtitleLabel = new QLabel(QStringLiteral("选择一位好友，打开或创建私聊会话"), card);
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setStyleSheet(QStringLiteral(
        "color: %1; background: transparent; border: none; font-size: 12px;")
        .arg(Theme::InkSoft.name()));
    titleColumn->addWidget(titleLabel);
    titleColumn->addWidget(subtitleLabel);
    titleRow->addLayout(titleColumn, 1);

    auto *closeButton = new QPushButton(QStringLiteral("×"), card);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setFixedSize(30, 30);
    closeButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; background: %2; border: none; border-radius: 15px; font-size: 19px; }"
        "QPushButton:hover { color: white; background: %3; }")
        .arg(Theme::InkSoft.name(), Theme::Nav.name(), Theme::CloseHover.name()));
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    titleRow->addWidget(closeButton, 0, Qt::AlignTop);
    layout->addLayout(titleRow);
    layout->addSpacing(22);

    auto *combo = new QComboBox(card);
    combo->setObjectName(QStringLiteral("privateChatFriendCombo"));
    combo->setMinimumHeight(42);
    combo->setCursor(Qt::PointingHandCursor);
    combo->setStyleSheet(QStringLiteral(
        "QComboBox { color: %1; background: %2; border: 1px solid #E1DBF1; border-radius: 10px; padding: 0 12px; }"
        "QComboBox:hover { border-color: #CFC3F5; }"
        "QComboBox:focus { border-color: %3; }"
        "QComboBox::drop-down { width: 30px; border: none; }"
        "QComboBox QAbstractItemView { color: %1; background: %2; border: 1px solid #E1DBF1;"
        " selection-background-color: %4; selection-color: %1; outline: none; padding: 5px; }")
        .arg(Theme::Ink.name(), Theme::White.name(), Theme::Purple.name(), Theme::ConversationActive.name()));
    for (const FriendChoice &choice : choices)
    {
        combo->addItem(choice.display, QVariant::fromValue<qulonglong>(choice.userId));
    }
    layout->addWidget(combo);
    layout->addSpacing(24);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(10);
    buttonRow->addStretch(1);
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), card);
    cancelButton->setCursor(Qt::PointingHandCursor);
    cancelButton->setFixedSize(86, 38);
    cancelButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; background: %2; border: 1px solid #E4DDF3; border-radius: 10px; font-weight: 600; }"
        "QPushButton:hover { background: %3; }")
        .arg(Theme::InkSoft.name(), Theme::Nav.name(), Theme::PurpleSoft.name()));
    auto *startButton = new QPushButton(QStringLiteral("打开会话"), card);
    startButton->setObjectName(QStringLiteral("startPrivateChatButton"));
    startButton->setCursor(Qt::PointingHandCursor);
    startButton->setFixedSize(112, 38);
    startButton->setStyleSheet(QStringLiteral(
        "QPushButton#startPrivateChatButton { color: white; background: %1; border: none; border-radius: 10px; font-weight: 650; }"
        "QPushButton#startPrivateChatButton:hover { background: %2; }"
        "QPushButton#startPrivateChatButton:pressed { background: #4935C8; }")
        .arg(Theme::Purple.name(), Theme::PurpleDeep.name()));
    buttonRow->addWidget(cancelButton);
    buttonRow->addWidget(startButton);
    layout->addLayout(buttonRow);
    outerLayout->addWidget(card);

    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(startButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.adjustSize();
    const QPoint parentCenter = m_mainWindow->mapToGlobal(m_mainWindow->rect().center());
    dialog.move(parentCenter.x() - dialog.width() / 2,
                parentCenter.y() - dialog.height() / 2);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const quint64 targetUserId = combo->currentData().toULongLong();
    if (targetUserId == 0)
    {
        m_mainWindow->setMessageStatus(QStringLiteral("所选好友 ID 无效，无法创建会话"), true);
        return;
    }

    openPrivateConversation(targetUserId);
}

void AppController::handleCreateConversationResponse(
    protocolHeader::CommandType responseCommand,
    const QByteArray &jsonBody)
{
    if (m_mainWindow == nullptr)
    {
        return;
    }
    if (responseCommand != protocolHeader::CommandType::PRIVATECREATE_SUCCESS &&
        responseCommand != protocolHeader::CommandType::PRIVATECREATE_FAILED)
    {
        m_mainWindow->setMessageStatus(QStringLiteral("服务器返回了无法识别的创建会话响应"), true);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        m_mainWindow->setMessageStatus(QStringLiteral("无法解析服务器返回的创建会话结果"), true);
        return;
    }
    const QJsonObject body = document.object();
    const QString serverMessage = body.value(QString::fromLatin1(protocolBody::MESSAGE))
                                      .toString(QStringLiteral("创建会话请求已完成"));
    if (responseCommand == protocolHeader::CommandType::PRIVATECREATE_FAILED)
    {
        m_mainWindow->setMessageStatus(serverMessage, true);
        return;
    }

    // 创建会话的旧响应格式暂时保留：conversation_id 仍在 JSON 最外层。
    // 等客户端/服务端一起迁移时，再统一改成 data.conversation_id。
    quint64 conversationId = 0;
    if (!readPositiveJsonInteger(body, protocolBody::CONVERSATION_ID, conversationId))
    {
        m_mainWindow->setMessageStatus(QStringLiteral("创建会话成功响应缺少有效 conversation_id"), true);
        return;
    }

    m_pendingConversationSelectionId = conversationId;
    requestConversationList();
    m_mainWindow->setMessageStatus(QStringLiteral("会话已创建/打开，正在刷新会话列表…"), false);
}

void AppController::onHistoryRequested(quint64 conversationId)
{
    if (!m_isAuthenticated || m_isLoggingOut || m_mainWindow == nullptr)
    {
        return;
    }
    if (conversationId == 0)
    {
        m_mainWindow->setMessageStatus(QStringLiteral("当前会话尚未绑定数据库 Conversation.id"), true);
        return;
    }
    if (m_historyLoadingConversations.contains(conversationId))
    {
        return; // 同一会话只允许一个历史页请求在途，防止重复点击造成分页竞态。
    }
    if (m_historyExhaustedConversations.contains(conversationId))
    {
        m_mainWindow->setHistoryLoading(conversationId, false, false);
        return;
    }

    const quint64 cursor = m_historyCursors.value(conversationId, 0);
    QJsonObject body;
    body.insert(QString::fromLatin1(protocolBody::CONVERSATION_ID),
                static_cast<qint64>(conversationId));
    body.insert(QString::fromLatin1(protocolBody::SEQ), static_cast<qint64>(cursor));

    const quint64 requestId = m_nextRequestId++;
    m_historyRequests.insert(requestId, conversationId);
    m_historyLoadingConversations.insert(conversationId);
    m_mainWindow->setHistoryLoading(conversationId, true, true);

    // 历史查询也必须有超时；网络异常时不能让按钮永久停留在“正在加载”。
    auto *historyTimer = new QTimer(this);
    historyTimer->setSingleShot(true);
    m_historyRequestTimers.insert(requestId, historyTimer);
    connect(historyTimer, &QTimer::timeout, this, [this, requestId, conversationId]()
    {
        m_historyRequests.remove(requestId);
        m_requestsAwaitingResponse.remove(requestId);
        m_historyRequestTimers.remove(requestId);
        m_historyLoadingConversations.remove(conversationId);
        if (auto *timer = qobject_cast<QTimer *>(sender()))
        {
            timer->deleteLater();
        }
        if (m_mainWindow != nullptr)
        {
            m_mainWindow->setHistoryLoading(conversationId, false, true);
            m_mainWindow->setMessageStatus(QStringLiteral("历史消息请求超时，请重试"), true);
        }
    });
    historyTimer->start(15000);

    const PendingRequest request{protocolHeader::CommandType::MESSAGE_HISTORY_REQUEST,
                                 requestId, body};
    if (!m_tcpClient->isConnected())
    {
        m_historyRequests.remove(requestId);
        m_historyLoadingConversations.remove(conversationId);
        if (auto *timer = m_historyRequestTimers.take(requestId)) { timer->stop(); timer->deleteLater(); }
        m_mainWindow->setHistoryLoading(conversationId, false, true);
        m_mainWindow->setMessageStatus(QStringLiteral("连接已断开，请重新登录后加载历史消息"), true);
        return;
    }
    if (!sendRequest(request))
    {
        m_historyRequests.remove(requestId);
        m_historyLoadingConversations.remove(conversationId);
        if (auto *timer = m_historyRequestTimers.take(requestId)) { timer->stop(); timer->deleteLater(); }
        m_mainWindow->setHistoryLoading(conversationId, false, true);
        m_mainWindow->setMessageStatus(QStringLiteral("历史消息请求发送失败"), true);
    }
}

void AppController::handleHistoryResponse(quint64 requestId,
                                          protocolHeader::CommandType responseCommand,
                                          const QByteArray &jsonBody)
{
    const quint64 conversationId = m_historyRequests.take(requestId);
    m_requestsAwaitingResponse.remove(requestId);
    m_historyLoadingConversations.remove(conversationId);
    if (auto *timer = m_historyRequestTimers.take(requestId))
    {
        timer->stop();
        timer->deleteLater();
    }

    if (m_mainWindow == nullptr)
    {
        return;
    }

    if (responseCommand != protocolHeader::CommandType::MESSAGE_HISTORY_SUCCESS &&
        responseCommand != protocolHeader::CommandType::MESSAGE_HISTORY_FAILED)
    {
        m_mainWindow->setHistoryLoading(conversationId, false, true);
        m_mainWindow->setMessageStatus(QStringLiteral("服务器返回了无法识别的历史消息响应"), true);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        m_mainWindow->setHistoryLoading(conversationId, false, true);
        m_mainWindow->setMessageStatus(QStringLiteral("无法解析服务器返回的历史消息"), true);
        return;
    }

    const QJsonObject body = document.object();
    const QString serverMessage = body.value(QString::fromLatin1(protocolBody::MESSAGE))
                                      .toString(QStringLiteral("历史消息查询完成"));
    if (responseCommand == protocolHeader::CommandType::MESSAGE_HISTORY_FAILED)
    {
        m_mainWindow->setHistoryLoading(conversationId, false, true);
        m_mainWindow->setMessageStatus(serverMessage, true);
        return;
    }

    const QJsonValue contentValue = body.value(QString::fromLatin1(protocolBody::CONTENT));
    const QJsonValue cursorValue = body.value(QString::fromLatin1(protocolBody::NEXT_CURSOR));
    if (!contentValue.isArray() || !cursorValue.isDouble())
    {
        m_mainWindow->setHistoryLoading(conversationId, false, true);
        m_mainWindow->setMessageStatus(QStringLiteral("历史消息响应缺少 content 或 next_cursor"), true);
        return;
    }

    quint64 nextCursor = 0;
    const qint64 cursorNumber = cursorValue.toInteger(-1);
    if (cursorNumber < 0)
    {
        m_mainWindow->setHistoryLoading(conversationId, false, true);
        m_mainWindow->setMessageStatus(QStringLiteral("历史消息分页游标无效"), true);
        return;
    }
    nextCursor = static_cast<quint64>(cursorNumber);

    QVector<ChatMessageRecord> historyMessages;
    quint64 smallestSequence = 0;
    const QJsonArray items = contentValue.toArray();
    for (const QJsonValue &itemValue : items)
    {
        if (!itemValue.isObject())
        {
            continue;
        }
        const QJsonObject item = itemValue.toObject();
        ChatMessageRecord message;
        message.conversationId = conversationId;
        if (!readPositiveJsonInteger(item, protocolBody::SEQ, message.sequence) ||
            !readPositiveJsonInteger(item, protocolBody::USER_ID, message.senderId) ||
            !item.value(QString::fromLatin1(protocolBody::USERNAME)).isString() ||
            !item.value(QString::fromLatin1(protocolBody::TEXT)).isString())
        {
            qWarning() << "跳过字段不完整的历史消息";
            continue;
        }
        message.senderName = item.value(QString::fromLatin1(protocolBody::USERNAME)).toString();
        message.content = item.value(QString::fromLatin1(protocolBody::TEXT)).toString();
        if (message.content.isEmpty() || message.content.toUcs4().size() > 512 ||
            message.content.toUtf8().size() > 2048 || message.senderName.trimmed().isEmpty())
        {
            continue;
        }
        message.timeText = QStringLiteral("历史消息");
        // 当前历史接口只返回 seq/user_id/username/text，没有 server_msg_id 与时间戳。
        // 模型用 conversation_id + seq 去重，后续可在不破坏协议兼容性的情况下补充字段。
        if (smallestSequence == 0 || message.sequence < smallestSequence)
        {
            smallestSequence = message.sequence;
        }
        historyMessages.push_back(message);
    }

    // 一次性把历史页交给聊天区处理；避免逐条插入时反复滚到底部，导致视口跳动。
    const int insertedCount = m_mainWindow->appendHistoryMessages(conversationId, historyMessages);

    if (nextCursor == 0 || items.isEmpty() || smallestSequence == 0)
    {
        m_historyExhaustedConversations.insert(conversationId);
    }
    else
    {
        // 服务端 next_cursor 是当前页最早消息的 seq；下一次查询严格小于该值。
        // 如果服务端游标没有前进，停止继续请求，防止异常响应造成死循环。
        const quint64 oldCursor = m_historyCursors.value(conversationId, 0);
        if (oldCursor != 0 && nextCursor >= oldCursor)
        {
            m_historyExhaustedConversations.insert(conversationId);
        }
        else
        {
            m_historyCursors.insert(conversationId, nextCursor);
        }
    }

    const bool hasMore = !m_historyExhaustedConversations.contains(conversationId);
    m_mainWindow->setHistoryLoading(conversationId, false, hasMore);
    m_mainWindow->setMessageStatus(
        insertedCount > 0 ? QStringLiteral("已加载 %1 条历史消息").arg(insertedCount)
                          : (hasMore ? QStringLiteral("没有新增历史消息") : QStringLiteral("历史消息已加载完毕")),
        false);
}

void AppController::onLoginSucceeded()
{
    // LoginWindow 的信号只能在服务器确认登录后触发，仍然检查状态以保护窗口切换。
    if (!m_isAuthenticated)
    {
        return;
    }

    if (m_mainWindow == nullptr)
    {
        m_mainWindow = new MainWindow;
        connect(m_mainWindow, &MainWindow::logoutRequested,
                this, &AppController::onLogoutRequested);
        connect(m_mainWindow, &MainWindow::historyRequested,
                this, &AppController::onHistoryRequested);
        connect(m_mainWindow, &MainWindow::messageSendRequested,
                this, &AppController::onMessageSendRequested);
        connect(m_mainWindow, &MainWindow::messageRetryRequested,
                this, &AppController::onMessageRetryRequested);
        connect(m_mainWindow, &MainWindow::newSessionRequested,
                this, &AppController::onNewSessionRequested);
        connect(m_mainWindow, &MainWindow::contactsRefreshRequested,
                this, &AppController::onContactsRefreshRequested);
        connect(m_mainWindow, &MainWindow::friendRequestSendRequested,
                this, &AppController::onFriendRequestSendRequested);
        connect(m_mainWindow, &MainWindow::friendRequestHandleRequested,
                this, &AppController::onFriendRequestHandleRequested);
        connect(m_mainWindow, &MainWindow::contactOpenPrivateChatRequested,
                this, &AppController::onContactOpenPrivateChatRequested);
    }

    m_mainWindow->clearSessions();
    m_mainWindow->setContacts({});
    m_mainWindow->setPendingFriendRequests({});
    m_mainWindow->setContactsStatus(QStringLiteral("进入联系人页以加载好友和收到的申请。"), false);
    m_mainWindow->finishAddFriendRequest(false);
    m_mainWindow->setCurrentUsername(m_currentAccount);
    setNetworkStatus(QStringLiteral("服务器连接正常"), QStringLiteral("connected"));
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
    m_loginWindow->hide();
    requestConversationList();
}

void AppController::stopMessageRequestTimer(quint64 requestId)
{
    auto timerIt = m_messageRequestTimers.find(requestId);
    if (timerIt == m_messageRequestTimers.end())
    {
        return;
    }

    QTimer *timer = timerIt.value();
    m_messageRequestTimers.erase(timerIt);
    timer->stop();
    timer->deleteLater();
}

void AppController::clearMessageRequestTimers()
{
    const auto timers = m_messageRequestTimers;
    m_messageRequestTimers.clear();
    for (QTimer *timer : timers)
    {
        if (timer != nullptr)
        {
            timer->stop();
            timer->deleteLater();
        }
    }
}

void AppController::startMessageRequestTimer(quint64 requestId, const QString &clientMessageId)
{
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    m_messageRequestTimers.insert(requestId, timer);
    connect(timer, &QTimer::timeout, this, [this, requestId, clientMessageId]()
    {
        const bool wasWaiting = m_requestsAwaitingResponse.remove(requestId) > 0;
        m_messageRequestClientIds.remove(requestId);
        m_messagesInFlight.remove(clientMessageId);
        stopMessageRequestTimer(requestId);

        // 如果收到自己的服务端推送，消息可能已被确认，超时回调不能再把它改回失败。
        if (wasWaiting && m_retryableMessages.contains(clientMessageId))
        {
            if (m_mainWindow != nullptr)
            {
                m_mainWindow->setMessageSendState(
                    clientMessageId, MessageSendState::Failed);
                m_mainWindow->setMessageStatus(
                    QStringLiteral("消息确认超时，点击失败消息可使用原 ID 重试"), true);
            }
        }
    });
    timer->start(15000);
}

void AppController::clearPendingRequests()
{
    m_authRequestPending = false;
    m_pendingRememberMe = false;
    m_isLoggingOut = false;
    m_requestTimer.stop();

    // 清理请求前先让仍在等待确认的气泡退出“发送中”，避免界面留下永久转圈状态。
    if (m_mainWindow != nullptr)
    {
        const auto requestClientIds = m_messageRequestClientIds;
        for (auto it = requestClientIds.cbegin(); it != requestClientIds.cend(); ++it)
        {
            m_mainWindow->setMessageSendState(it.value(), MessageSendState::Failed);
        }
    }

    clearMessageRequestTimers();
    clearOperationRequestTimers();
    m_messageRequestClientIds.clear();
    m_messagesInFlight.clear();
    m_pendingRequests.clear();
    m_requestsAwaitingResponse.clear();
}

void AppController::returnToLoginWindow()
{
    // 不让上一账号未完成的会话列表/好友列表/消息请求在下次登录后更新界面。
    clearPendingRequests();
    m_retryableMessages.clear();
    m_messageRequestClientIds.clear();
    m_messagesInFlight.clear();
    for (auto it = m_historyRequests.cbegin(); it != m_historyRequests.cend(); ++it)
    {
        m_requestsAwaitingResponse.remove(it.key());
    }
    m_historyRequests.clear();
    for (auto *timer : m_historyRequestTimers)
    {
        if (timer != nullptr)
        {
            timer->stop();
            timer->deleteLater();
        }
    }
    m_historyRequestTimers.clear();
    m_historyCursors.clear();
    m_historyLoadingConversations.clear();
    m_historyExhaustedConversations.clear();
    // 此方法不负责修改登录状态；调用者应先更新 m_isAuthenticated 和用户信息。
    if (m_mainWindow != nullptr)
    {
        // 主窗口会在下次登录时复用，但消息和会话列表不能跨账号保留。
        m_mainWindow->clearMessages();
        m_mainWindow->clearSessions();
        m_mainWindow->hide();
    }

    if (m_loginWindow != nullptr)
    {
        // 重新登录时回到简洁欢迎页，并恢复欢迎页原有尺寸。
        m_loginWindow->resetToWelcomePage();
        m_loginWindow->show();
        m_loginWindow->raise();
        m_loginWindow->activateWindow();
    }
}

void AppController::setNetworkStatus(const QString &message, const QString &state)
{
    m_networkStatusState = state;
    if (m_loginWindow != nullptr)
    {
        m_loginWindow->setNetworkStatus(message, state);
    }
    if (m_mainWindow != nullptr)
    {
        m_mainWindow->setNetworkStatus(message, state);
    }
}

void AppController::showResponseMessage(const QString &title,
                                        const QString &message,
                                        bool isError)
{
    QWidget *parentWidget = m_loginWindow;
    if (m_mainWindow != nullptr && m_mainWindow->isVisible())
    {
        parentWidget = m_mainWindow;
    }

    // 继续使用项目的紫灰主题提示框，不使用原生 QMessageBox。
    ModernMessageBox::showMessage(parentWidget, title, message, isError);
}
