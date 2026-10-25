#include "AppController.h"

#include "network/TcpClient.h"
#include "ui/LoginWindow.h"
#include "ui/MainWindow.h"
#include "ui/ModernMessageBox.h"
#include "ui/SessionListPanel.h"
#include "Theme.h"
#include "protocol/ProtocolBody.h"
#include "model/ChatMessageRecord.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QInputDialog>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QWidget>
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
    if (m_tcpClient->isConnected() || m_isConnecting || m_serverHost.isEmpty())
    {
        return;
    }

    m_isConnecting = true;
    m_tcpClient->connectToServer(m_serverHost, m_serverPort);
}

void AppController::onConnectedToServer()
{
    m_isConnecting = false;
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
        showResponseMessage(
            QStringLiteral("连接已断开"),
            QStringLiteral("与服务器的连接已断开，已返回登录界面，请重新登录。"),
            true);
    }
    else if (wasAuthenticationPending)
    {
        showResponseMessage(
            QStringLiteral("连接已断开"),
            QStringLiteral("请求完成前连接已断开，请检查网络后重试。"),
            true);
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
        m_tcpClient->abortConnection();
        showResponseMessage(
            QStringLiteral("连接已断开"),
            message + QStringLiteral("\n\n客户端已返回登录界面，请重新登录。"),
            true);
    }
    else if (hadPendingRequest)
    {
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

    const auto responseCommand = static_cast<protocolHeader::CommandType>(command);

    const bool isMessageSendRequest =
        originalCommand == protocolHeader::CommandType::GROUP_MESSAGE_SEND ||
        originalCommand == protocolHeader::CommandType::PRIVATE_MESSAGE_SEND;
    if (isMessageSendRequest)
    {
        stopMessageRequestTimer(requestId);

        if (responseCommand != protocolHeader::CommandType::MESSAGE_SEND_SUCCESS &&
            responseCommand != protocolHeader::CommandType::MESSAGE_SEND_FAILED)
        {
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
                m_mainWindow->setMessageStatus(QStringLiteral("消息已保存到服务器"), false);
                requestConversationList(m_mainWindow->currentConversationId());
            }
            else
            {
                m_mainWindow->setMessageStatus(resultMessage, true);
            }
        }
        return;
    }

    // 会话列表请求独立处理，不混入登录/注册/退出登录状态机。
    if (originalCommand == protocolHeader::CommandType::QUERY_CONVERSATION_LIST_REQUEST)
    {
        const quint64 preferredConversationId = m_conversationListPreferredIds.take(requestId);
        if (responseCommand != protocolHeader::CommandType::QUERY_CONVERSATION_LIST_SUCCESS)
        {
            QJsonParseError error;
            const QJsonDocument responseDoc = QJsonDocument::fromJson(jsonBody, &error);
            const QString message = error.error == QJsonParseError::NoError && responseDoc.isObject()
                ? responseDoc.object().value(QString::fromLatin1(protocolBody::MESSAGE))
                      .toString(QStringLiteral("查询会话列表失败"))
                : QStringLiteral("服务器返回了无法识别的会话列表响应");
            if (m_mainWindow != nullptr)
            {
                m_mainWindow->setMessageStatus(message, true);
            }
            return;
        }

        QJsonParseError error;
        const QJsonDocument responseDoc = QJsonDocument::fromJson(jsonBody, &error);
        if (error.error != QJsonParseError::NoError || !responseDoc.isObject())
        {
            if (m_mainWindow != nullptr)
            {
                m_mainWindow->setMessageStatus(QStringLiteral("无法解析服务器的会话列表"), true);
            }
            return;
        }

        const QJsonObject response = responseDoc.object();
        const QJsonValue dataValue = response.value(QString::fromLatin1(protocolBody::DATA));
        if (!dataValue.isArray())
        {
            if (m_mainWindow != nullptr)
            {
                m_mainWindow->setMessageStatus(QStringLiteral("会话列表响应缺少 data 数组"), true);
            }
            return;
        }

        QVector<SessionItem> sessions;
        const QJsonArray items = dataValue.toArray();
        for (const QJsonValue &itemValue : items)
        {
            if (!itemValue.isObject())
            {
                continue;
            }

            const QJsonObject item = itemValue.toObject();
            quint64 conversationId = 0;
            if (!readPositiveJsonInteger(item, protocolBody::CONVERSATION_ID, conversationId))
            {
                qWarning() << "跳过缺少有效 conversation_id 的会话列表项";
                continue;
            }

            const int type = item.value(QString::fromLatin1(protocolBody::TYPE)).toInt(0);
            if (type != 1 && type != 2)
            {
                qWarning() << "跳过无法识别的会话类型:" << type;
                continue;
            }

            QString displayName = item.value(QString::fromLatin1(protocolBody::DISPLAY_NAME)).toString().trimmed();
            if (displayName.isEmpty())
            {
                displayName = item.value(QString::fromLatin1(protocolBody::USERNAME)).toString().trimmed();
            }
            if (displayName.isEmpty())
            {
                displayName = type == 2 ? QStringLiteral("未命名群聊") : QStringLiteral("未知联系人");
            }

            SessionItem session;
            session.id = QString::number(conversationId);
            session.conversationId = conversationId;
            session.name = displayName;
            session.avatarText = displayName.left(1);
            session.group = (type == 2);
            session.unreadCount = 0; // ReadCursor 尚未纳入本阶段，不伪造未读数字。
            session.unread = false;

            const QString lastMessage = item.value(QString::fromLatin1(protocolBody::LAST_MESSAGE)).toString();
            session.preview = lastMessage.trimmed().isEmpty()
                ? QStringLiteral("还没有消息") : lastMessage;

            const QString rawTime = item.value(QString::fromLatin1(protocolBody::LAST_TIME)).toString();
            const QDateTime dateTime = QDateTime::fromString(rawTime, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            if (dateTime.isValid())
            {
                if (dateTime.date() == QDate::currentDate())
                {
                    session.time = dateTime.toString(QStringLiteral("HH:mm"));
                }
                else if (dateTime.date().year() == QDate::currentDate().year())
                {
                    session.time = dateTime.toString(QStringLiteral("MM-dd"));
                }
                else
                {
                    session.time = dateTime.toString(QStringLiteral("yyyy-MM-dd"));
                }
            }
            else
            {
                session.time = rawTime;
            }

            // 头像暂时由首字和主题色绘制；服务端头像 URL 已保留在协议中，后续再接图片缓存。
            if (session.group)
            {
                session.avatarStart = Theme::DarkStart;
                session.avatarEnd = Theme::DarkEnd;
            }
            else
            {
                const int colorIndex = sessions.size() % 4;
                switch (colorIndex)
                {
                case 0: session.avatarStart = Theme::PeachStart; session.avatarEnd = Theme::PeachEnd; break;
                case 1: session.avatarStart = Theme::MintStart; session.avatarEnd = Theme::MintEnd; break;
                case 2: session.avatarStart = Theme::BlueStart; session.avatarEnd = Theme::BlueEnd; break;
                default: session.avatarStart = Theme::GoldStart; session.avatarEnd = Theme::GoldEnd; break;
                }
            }
            sessions.push_back(session);
        }

        if (m_mainWindow != nullptr)
        {
            m_mainWindow->setConversations(sessions, preferredConversationId);
            if (sessions.isEmpty())
            {
                m_mainWindow->setMessageStatus(
                    QStringLiteral("还没有会话，点击列表上方的“新建会话”可与好友开始聊天。"), false);
            }
            else
            {
                m_mainWindow->setMessageStatus(
                    QStringLiteral("已同步 %1 个真实会话").arg(sessions.size()), false);
            }
        }
        return;
    }

    // 点击“新建会话”后先查询好友；用户选择好友后才请求服务端创建/获取私聊。
    if (originalCommand == protocolHeader::CommandType::QUERY_FRIEND_LIST_REQUEST)
    {
        if (responseCommand != protocolHeader::CommandType::QUERY_FRIEND_LIST_SUCCESS)
        {
            QJsonParseError error;
            const QJsonDocument responseDoc = QJsonDocument::fromJson(jsonBody, &error);
            const QString message = error.error == QJsonParseError::NoError && responseDoc.isObject()
                ? responseDoc.object().value(QString::fromLatin1(protocolBody::MESSAGE))
                      .toString(QStringLiteral("查询好友列表失败"))
                : QStringLiteral("服务器返回了无法识别的好友列表响应");
            if (m_mainWindow != nullptr)
            {
                m_mainWindow->setMessageStatus(message, true);
            }
            return;
        }

        QJsonParseError error;
        const QJsonDocument responseDoc = QJsonDocument::fromJson(jsonBody, &error);
        if (error.error != QJsonParseError::NoError || !responseDoc.isObject())
        {
            if (m_mainWindow != nullptr)
            {
                m_mainWindow->setMessageStatus(QStringLiteral("无法解析服务器的好友列表"), true);
            }
            return;
        }

        const QJsonValue dataValue = responseDoc.object().value(QString::fromLatin1(protocolBody::DATA));
        if (!dataValue.isArray())
        {
            if (m_mainWindow != nullptr)
            {
                m_mainWindow->setMessageStatus(QStringLiteral("好友列表响应缺少 data 数组"), true);
            }
            return;
        }

        QStringList friendLabels;
        QHash<QString, quint64> friendIdsByLabel;
        for (const QJsonValue &itemValue : dataValue.toArray())
        {
            if (!itemValue.isObject())
            {
                continue;
            }
            const QJsonObject item = itemValue.toObject();
            quint64 friendId = 0;
            if (!readPositiveJsonInteger(item, protocolBody::USER_ID, friendId))
            {
                continue;
            }
            const QString username = item.value(QString::fromLatin1(protocolBody::USERNAME)).toString().trimmed();
            QString nickname = item.value(QString::fromLatin1(protocolBody::NICKNAME)).toString().trimmed();
            if (nickname.isEmpty())
            {
                nickname = username;
            }
            if (username.isEmpty())
            {
                continue;
            }
            const QString label = QStringLiteral("%1（%2）").arg(nickname, username);
            friendLabels.push_back(label);
            friendIdsByLabel.insert(label, friendId);
        }

        if (friendLabels.isEmpty())
        {
            if (m_mainWindow != nullptr)
            {
                m_mainWindow->setMessageStatus(
                    QStringLiteral("当前好友列表为空，请先添加好友，再新建私聊会话。"), true);
            }
            return;
        }

        // 使用项目的紫灰主题，避免好友选择弹窗与主界面风格差异过大。
        QInputDialog dialog(m_mainWindow);
        dialog.setWindowTitle(QStringLiteral("新建会话"));
        dialog.setLabelText(QStringLiteral("选择要开始聊天的好友："));
        dialog.setComboBoxItems(friendLabels);
        dialog.setComboBoxEditable(false);
        dialog.setStyleSheet(QStringLiteral(
            "QInputDialog { background: #FCFBFF; color: #302A48; }"
            "QLabel { color: #302A48; font-size: 13px; }"
            "QComboBox { background: #FFFFFF; color: #302A48; border: 1px solid rgba(109,88,172,0.24); border-radius: 9px; padding: 8px 10px; min-width: 250px; }"
            "QComboBox::drop-down { border: 0; width: 25px; }"
            "QComboBox QAbstractItemView { background: #FFFFFF; color: #302A48; selection-background-color: #ECE6FF; selection-color: #302A48; outline: 0; }"
            "QPushButton { background: #6A53F6; color: #FFFFFF; border: 0; border-radius: 9px; padding: 8px 18px; min-width: 70px; }"
            "QPushButton:hover { background: #5941E7; }"
            "QPushButton:pressed { background: #4C37CF; }"));
        if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }
        const QString selectedLabel = dialog.textValue().trimmed();
        if (selectedLabel.isEmpty())
        {
            return;
        }

        const quint64 targetUserId = friendIdsByLabel.value(selectedLabel, 0);
        if (targetUserId == 0)
        {
            m_mainWindow->setMessageStatus(QStringLiteral("所选好友信息无效，请重试。"), true);
            return;
        }

        QJsonObject createBody;
        createBody.insert(QString::fromLatin1(protocolBody::TYPE), 1);
        createBody.insert(QString::fromLatin1(protocolBody::TARGET_USER_ID),
                          QJsonValue(static_cast<qint64>(targetUserId)));
        queueOrSendRequest(protocolHeader::CommandType::CHATCREATE_REQUEST, createBody);
        m_mainWindow->setMessageStatus(QStringLiteral("正在打开好友会话……"), false);
        return;
    }

    if (originalCommand == protocolHeader::CommandType::CHATCREATE_REQUEST)
    {
        if (responseCommand != protocolHeader::CommandType::PRIVATECREATE_SUCCESS)
        {
            QJsonParseError error;
            const QJsonDocument responseDoc = QJsonDocument::fromJson(jsonBody, &error);
            const QString message = error.error == QJsonParseError::NoError && responseDoc.isObject()
                ? responseDoc.object().value(QString::fromLatin1(protocolBody::MESSAGE))
                      .toString(QStringLiteral("创建/打开会话失败"))
                : QStringLiteral("服务器返回了无法识别的会话创建响应");
            if (m_mainWindow != nullptr)
            {
                m_mainWindow->setMessageStatus(message, true);
            }
            return;
        }

        QJsonParseError error;
        const QJsonDocument responseDoc = QJsonDocument::fromJson(jsonBody, &error);
        quint64 conversationId = 0;
        if (error.error != QJsonParseError::NoError || !responseDoc.isObject() ||
            !readPositiveJsonInteger(responseDoc.object(), protocolBody::CONVERSATION_ID, conversationId))
        {
            if (m_mainWindow != nullptr)
            {
                m_mainWindow->setMessageStatus(QStringLiteral("会话已创建，但服务器未返回有效 conversation_id"), true);
            }
            return;
        }
        requestConversationList(conversationId);
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
            if (m_tcpClient->isConnected())
            {
                m_tcpClient->abortConnection();
            }
        }
        m_pendingLoginAccount.clear();
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
            if (m_tcpClient->isConnected())
            {
                m_tcpClient->abortConnection();
            }
        }
        m_pendingLoginAccount.clear();
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
        m_loginWindow->notifyLoginSucceeded();
        break;

    case protocolHeader::CommandType::LOGIN_FAILED:
        m_pendingLoginAccount.clear();
        showResponseMessage(QStringLiteral("登录失败"), message, true);
        break;

    case protocolHeader::CommandType::REGISTER_SUCCESS:
        showResponseMessage(QStringLiteral("注册成功"), message, false);
        break;

    case protocolHeader::CommandType::REGISTER_FAILED:
        showResponseMessage(QStringLiteral("注册失败"), message, true);
        break;

    case protocolHeader::CommandType::LOGOUT_SUCCESS:
        // 只有服务端确认退出成功，才清除会话并显示登录窗口。
        m_isAuthenticated = false;
        m_currentAccount.clear();
        m_pendingLoginAccount.clear();
        returnToLoginWindow();
        showResponseMessage(QStringLiteral("退出登录成功"), message, false);
        break;

    case protocolHeader::CommandType::LOGOUT_FAILED:
        // 服务器拒绝退出时，保留当前登录状态，用户仍可以在主界面继续操作。
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
    if (m_mainWindow == nullptr || !m_isAuthenticated)
    {
        if (m_mainWindow != nullptr)
        {
            m_mainWindow->setMessageStatus(QStringLiteral("请先登录后再发送消息"), true);
        }
        return;
    }

    // 断线后的登录状态会由连接事件清理；这里再做一次防御检查，避免向失效连接写数据。
    if (!m_tcpClient->isConnected())
    {
        m_mainWindow->setMessageStatus(QStringLiteral("连接已断开，请重新登录"), true);
        return;
    }

    // conversationId 为 0 表示尚未选择有效的数据库会话，不能猜测目标会话。
    if (conversationId == 0)
    {
        m_mainWindow->setMessageStatus(
            QStringLiteral("当前没有选中真实会话，请从左侧列表选择或新建会话。"), true);
        return;
    }

    const QString content = text.trimmed();
    const QByteArray utf8Content = content.toUtf8();
    if (content.trimmed().isEmpty())
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

    QJsonObject body;
    body.insert(QString::fromLatin1(protocolBody::CONTENT), content);
    body.insert(QString::fromLatin1(protocolBody::CONVERSATION_ID),
                QJsonValue(static_cast<qint64>(conversationId)));
    body.insert(QString::fromLatin1(protocolBody::CLIENT_MSG_ID),
                QUuid::createUuid().toString(QUuid::WithoutBraces));
    body.insert(QString::fromLatin1(protocolBody::TYPE), 1); // 1 表示文本消息。

    const quint64 requestId = m_nextRequestId++;
    const auto command = isGroup
        ? protocolHeader::CommandType::GROUP_MESSAGE_SEND
        : protocolHeader::CommandType::PRIVATE_MESSAGE_SEND;
    const PendingRequest request{command, requestId, body};

    if (!sendRequest(request))
    {
        m_mainWindow->setMessageStatus(QStringLiteral("消息发送失败，请检查网络连接"), true);
        return;
    }

    // 每条消息单独等待服务器确认，避免用户连续发送时超时互相影响。
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, requestId]()
    {
        const bool wasWaiting = m_requestsAwaitingResponse.remove(requestId) > 0;
        stopMessageRequestTimer(requestId);
        if (wasWaiting && m_mainWindow != nullptr)
        {
            m_mainWindow->setMessageStatus(
                QStringLiteral("消息发送超时，服务器没有返回确认"), true);
        }
    });
    m_messageRequestTimers.insert(requestId, timer);
    timer->start(15000);

    // 只有本地 TCP 层接受了数据包，才把这条文本视作已提交发送。
    m_mainWindow->clearMessageInput();
    m_mainWindow->setMessageStatus(QStringLiteral("消息发送中…"), false);
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
        // 消息预览和列表顺序来自服务端数据库，收到新消息后刷新它们。
        requestConversationList(m_mainWindow->currentConversationId());
    }

    Q_UNUSED(command); // 私聊/群聊命令只用来区分协议事件，气泡样式不依赖会话类型。
}

void AppController::requestConversationList(quint64 preferredConversationId)
{
    if (!m_isAuthenticated || m_isLoggingOut || m_mainWindow == nullptr)
    {
        return;
    }

    const quint64 requestId = m_nextRequestId++;
    m_conversationListPreferredIds.insert(requestId, preferredConversationId);
    const PendingRequest request{
        protocolHeader::CommandType::QUERY_CONVERSATION_LIST_REQUEST,
        requestId,
        QJsonObject{}};

    if (m_tcpClient->isConnected())
    {
        if (!sendRequest(request))
        {
            m_conversationListPreferredIds.remove(requestId);
        }
        return;
    }

    m_pendingRequests.enqueue(request);
    ensureServerConnection();
}

void AppController::onNewSessionRequested()
{
    if (!m_isAuthenticated || m_isLoggingOut || m_mainWindow == nullptr)
    {
        return;
    }

    if (!m_tcpClient->isConnected())
    {
        m_mainWindow->setMessageStatus(QStringLiteral("连接已断开，请重新登录后再新建会话。"), true);
        return;
    }

    queueOrSendRequest(protocolHeader::CommandType::QUERY_FRIEND_LIST_REQUEST, QJsonObject{});
    m_mainWindow->setMessageStatus(QStringLiteral("正在查询好友列表……"), false);
}

void AppController::onConversationSelected(quint64 conversationId)
{
    if (!m_isAuthenticated || m_isLoggingOut || conversationId == 0 ||
        m_historyCursors.contains(conversationId) ||
        m_historyExhaustedConversations.contains(conversationId) ||
        m_historyLoadingConversations.contains(conversationId))
    {
        return;
    }

    // 每个会话第一次被打开时自动加载最新一页；历史按钮仍用于继续向前翻页。
    onHistoryRequested(conversationId);
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

    int insertedCount = 0;
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
        if (m_mainWindow->appendMessage(message))
        {
            ++insertedCount;
        }
    }

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
        connect(m_mainWindow, &MainWindow::conversationSelected,
                this, &AppController::onConversationSelected);
        connect(m_mainWindow, &MainWindow::newSessionRequested,
                this, &AppController::onNewSessionRequested);
    }

    m_mainWindow->setCurrentUsername(m_currentAccount);
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
    m_loginWindow->hide();

    // 登录成功后用当前连接的认证身份查询真实会话，不再展示硬编码示例项。
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

void AppController::clearPendingRequests()
{
    m_authRequestPending = false;
    m_isLoggingOut = false;
    m_requestTimer.stop();
    clearMessageRequestTimers();
    m_pendingRequests.clear();
    m_requestsAwaitingResponse.clear();
    m_conversationListPreferredIds.clear();
}

void AppController::returnToLoginWindow()
{
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
    m_pendingRequests.clear();
    m_requestsAwaitingResponse.clear();
    m_conversationListPreferredIds.clear();
    clearMessageRequestTimers();
    // 此方法不负责修改登录状态；调用者应先更新 m_isAuthenticated 和用户信息。
    if (m_mainWindow != nullptr)
    {
        // 主窗口会在下次登录时复用，但内存消息不能跨账号保留。
        m_mainWindow->clearMessages();
        m_mainWindow->setConversations(QVector<SessionItem>{});
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
