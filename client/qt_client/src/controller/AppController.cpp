#include "AppController.h"

#include "network/TcpClient.h"
#include "ui/LoginWindow.h"
#include "ui/MainWindow.h"
#include "protocol/ProtocolBody.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSettings>
#include <QWidget>

AppController::AppController(QObject *parent)
    : QObject(parent),
      // TcpClient 以 AppController 为父对象，生命周期交给 Qt 的 QObject 父子机制管理。
      m_tcpClient(new TcpClient(this)),
      // LoginWindow 是顶层窗口，不设置 QWidget 父对象。
      m_loginWindow(new LoginWindow)
{
    // 认证请求最多等待 15 秒。超时后允许用户再次发起登录/注册。
    m_authRequestTimer.setSingleShot(true);
    connect(&m_authRequestTimer, &QTimer::timeout, this, [this]()
    {
        m_authRequestPending = false;
        m_pendingRequests.clear();
        m_requestsAwaitingResponse.clear();

        showResponseMessage(
            QStringLiteral("服务器响应超时"),
            QStringLiteral("服务器暂时没有返回登录/注册结果，请检查服务端后重试。"),
            true);
    });

    // 登录窗口只负责采集用户输入并发出信号，不直接访问网络。
    // 登录请求由 AppController 接手，避免 UI 类和 TCP 层相互耦合。
    connect(m_loginWindow, &LoginWindow::loginRequested,
            this, &AppController::onLoginRequested);
    connect(m_loginWindow, &LoginWindow::registerRequested,
            this, &AppController::onRegisterRequested);

    // LoginWindow 只有在业务层确认登录成功后才会发出 loginSucceeded。
    // 收到这个信号后，AppController 才会显示聊天主窗口。
    connect(m_loginWindow, &LoginWindow::loginSucceeded,
            this, &AppController::onLoginSucceeded);

    // TCP 连接成功、断开或发生错误时，由 TcpClient 通过信号通知控制器。
    connect(m_tcpClient, &TcpClient::connectedToServer,
            this, &AppController::onConnectedToServer);
    connect(m_tcpClient, &TcpClient::disconnectedFromServer,
            this, &AppController::onDisconnectedFromServer);
    connect(m_tcpClient, &TcpClient::connectionError,
            this, &AppController::onConnectionError);

    // 收到无法处理的数据包时，提示协议错误。
    // 这里使用 lambda 是因为本次只做简单的转发，不需要再单独声明一个槽函数。
    connect(m_tcpClient, &TcpClient::protocolError,
            this, [this](const QString &message)
    {
        // 协议错误意味着当前数据无法继续按正常请求处理。
        // 清理认证状态，避免随后断开连接时再次弹出重复提示。
        m_authRequestPending = false;
        m_authRequestTimer.stop();
        m_pendingRequests.clear();
        m_requestsAwaitingResponse.clear();

        showResponseMessage(QStringLiteral("通信协议错误"), message, true);
    });

    // TcpClient 已经完成 TCP 数据读取和协议拆包；控制器只处理完整的数据包。
    connect(m_tcpClient, &TcpClient::packetReceived,
            this, &AppController::onPacketReceived);

    // 应用启动后先显示欢迎/登录窗口。
    m_loginWindow->show();

    // 启动时只读取并验证配置，不立即连接服务器。
    // 这样用户打开客户端时，即使服务端还没启动，也不会立刻被连接错误弹窗打断。
    // 真正提交登录或注册时，queueOrSendRequest() 才会按需建立连接。
    m_configurationValid = loadClientConfig();
}

AppController::~AppController()
{
    // LoginWindow 和 MainWindow 都是独立的顶层窗口，不会因为本对象销毁而自动释放。
    // 因此需要在析构函数中手动删除它们。
    delete m_mainWindow;
    delete m_loginWindow;

    // 不需要手动 delete m_tcpClient：它以 this 为 QObject 父对象，基类析构时会自动释放。
}

bool AppController::loadClientConfig()
{
    // applicationDirPath() 返回可执行文件所在目录。
    // CMake 会把 config/client.conf 复制到这个目录，运行时便不依赖当前工作目录。
    const QString configPath =
        QCoreApplication::applicationDirPath() + QStringLiteral("/client.conf");
    QSettings settings(configPath, QSettings::IniFormat);

    // 如果配置文件中没有该项，则默认使用本机地址 127.0.0.1。
    // 若服务端运行在另一台主机/虚拟机上，需要把这里的配置改为服务端实际 IP。
    m_serverHost = settings.value(QStringLiteral("SERVER_HOST"),
                                  QStringLiteral("192.168.230.128")).toString().trimmed();

    bool portOk = false;
    // toInt 的第二个参数用于接收转换是否成功的结果。
    const int port = settings.value(QStringLiteral("SERVER_PORT"), 8888).toInt(&portOk);

    // 合法端口必须在 1 到 65535 之间；主机地址不能为空。
    if (m_serverHost.isEmpty() || !portOk || port < 1 || port > 65535)
    {
        showResponseMessage(
            QStringLiteral("客户端配置错误"),
            QStringLiteral("client.conf 中的 SERVER_HOST 或 SERVER_PORT 无效。"),
            true);
        return false;
    }

    // 上面已确认端口范围合法，所以可以安全转换为 quint16。
    m_serverPort = static_cast<quint16>(port);
    return true;
}

void AppController::onLoginRequested(const QString &account,
                                     const QString &password,
                                     bool rememberMe)
{
    // 当前服务端协议还没有处理“记住我”的字段，因此暂时不将它发送给服务器。
    // Q_UNUSED 用于明确告诉编译器：这个参数当前有意未使用。
    Q_UNUSED(rememberMe);

    // 配置无效时不排队请求，否则没有有效服务器地址可供连接。
    if (!m_configurationValid)
    {
        showResponseMessage(QStringLiteral("无法登录"),
                            QStringLiteral("请先检查客户端配置文件 client.conf。"), true);
        return;
    }

    // 一次登录/注册还未返回结果时，不允许重复提交另一个认证请求。
    if (m_authRequestPending)
    {
        showResponseMessage(QStringLiteral("请求处理中"),
                            QStringLiteral("登录或注册请求正在处理中，请稍候。"), false);
        return;
    }

    // 去掉账号首尾的空白，避免用户意外输入空格导致登录失败。
    // 密码不做 trim，因为密码中的空格可能是用户实际设置的内容。
    const QString normalizedAccount = account.trimmed();
    if (normalizedAccount.isEmpty() || password.isEmpty())
    {
        showResponseMessage(QStringLiteral("无法登录"),
                            QStringLiteral("请输入账号和密码。"), true);
        return;
    }

    // QJsonObject 用于构造 JSON 请求体，TcpClient 会把它序列化为 UTF-8 JSON。
    QJsonObject body;

    // UI 使用 account 这个名称表示账号，但当前服务端的登录校验实际读取 username。
    // 因此同时写入 account 和协议要求的 username：当前服务端读取 username，account 作为兼容字段保留。
    body.insert(QStringLiteral("account"), normalizedAccount);
    body.insert(QString::fromLatin1(protocolBody::USERNAME), normalizedAccount);

    // 密码字段名必须与公共协议中的 PASSWORD 保持一致。
    body.insert(QString::fromLatin1(protocolBody::PASSWORD), password);

    // 标记认证请求处理中，避免重复点击登录。
    // 如果发送失败或连接中断，相关错误处理会清除此状态。
    m_authRequestPending = true;
    m_authRequestTimer.start(15000);

    // 命令类型告诉服务器这是登录请求；requestId 由后续发送流程分配。
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

    if (m_authRequestPending)
    {
        showResponseMessage(QStringLiteral("请求处理中"),
                            QStringLiteral("登录或注册请求正在处理中，请稍候。"), false);
        return;
    }

    const QString normalizedAccount = account.trimmed();
    if (normalizedAccount.isEmpty() || password.isEmpty())
    {
        showResponseMessage(QStringLiteral("无法注册"),
                            QStringLiteral("请输入账号和密码。"), true);
        return;
    }

    // 注册请求同样使用 JSON 保存各字段。
    QJsonObject body;
    body.insert(QStringLiteral("account"), normalizedAccount);
    body.insert(QString::fromLatin1(protocolBody::USERNAME), normalizedAccount);
    body.insert(QString::fromLatin1(protocolBody::PASSWORD), password);

    // 当前服务端注册接口还要求 nickname，但目前的注册界面只有账号和密码两个输入项。
    // 这里暂时用账号作为初始昵称；日后增加昵称输入框，再把这个值换成用户填写的昵称。
    body.insert(QString::fromLatin1(protocolBody::NICKNAME), normalizedAccount);

    // 与登录共用一个认证请求状态，避免同时发送登录和注册请求。
    m_authRequestPending = true;
    m_authRequestTimer.start(15000);
    queueOrSendRequest(protocolHeader::CommandType::REGISTER_REQUEST, body);
}

void AppController::queueOrSendRequest(protocolHeader::CommandType command,
                                       const QJsonObject &body)
{
    // 为这个请求分配一个请求编号，并把命令和 JSON 内容一起保存。
    // 编号会写进协议头，服务器响应时可以带回相同编号。
    PendingRequest request{command, m_nextRequestId++, body};

    // 如果 TCP 已经连接，马上发送即可。
    if (m_tcpClient->isConnected())
    {
        sendRequest(request);
        return;
    }

    // TCP 连接建立是异步的，点击登录时连接可能还没有成功。
    // 先把请求放入队列，连接成功后再发送，避免请求因为“尚未连接”而丢失。
    m_pendingRequests.enqueue(request);

    // 如果尚未开始连接，发起连接；若已经连接中则不会重复连接。
    ensureServerConnection();
}

bool AppController::sendRequest(const PendingRequest &request)
{
    // sendRequest 内部会把 JSON 转成字节、加上协议头，再通过 QTcpSocket::write 发送。
    // 如果连接已经断开或编码/写入失败，TcpClient 会返回 false。
    if (!m_tcpClient->sendRequest(request.command, request.requestId, request.body))
    {
        // TcpClient 在发送失败时会发出 connectionError 或 protocolError，
        // 由对应信号统一显示错误。这里清理状态，避免重复弹出两个提示框。
        m_authRequestPending = false;
        m_authRequestTimer.stop();
        m_pendingRequests.clear();
        m_requestsAwaitingResponse.clear();
        return false;
    }

    // 只有发送成功后才登记等待响应的请求。服务器应在响应头中原样返回 requestId。
    m_requestsAwaitingResponse.insert(request.requestId, request.command);
    return true;
}

void AppController::ensureServerConnection()
{
    // 已连接、正在连接或配置中没有服务器地址时，都不再发起新的连接。
    if (m_tcpClient->isConnected() || m_isConnecting || m_serverHost.isEmpty())
    {
        return;
    }

    // 先标记为连接中，防止短时间内多次调用造成重复连接。
    m_isConnecting = true;

    // 这是异步连接：函数会很快返回，连接成功/失败由相应信号通知控制器。
    m_tcpClient->connectToServer(m_serverHost, m_serverPort);
}

void AppController::onConnectedToServer()
{
    // 收到 connectedToServer 表示 TCP 连接已经建立。
    m_isConnecting = false;

    // 把连接建立前暂存的登录/注册请求按先进先出顺序发出去。
    sendPendingRequests();
}

void AppController::onDisconnectedFromServer()
{
    // 连接断开后允许后续请求再次触发连接。
    m_isConnecting = false;

    // 如果请求发出后连接突然断开，当前请求就无法得到响应。
    // 清理状态，避免界面一直处于“请求处理中”而不能再次登录。
    if (m_authRequestPending)
    {
        m_authRequestPending = false;
        m_authRequestTimer.stop();
        m_pendingRequests.clear();
        m_requestsAwaitingResponse.clear();

        showResponseMessage(
            QStringLiteral("连接已断开"),
            QStringLiteral("与服务器的连接已断开，请检查网络后重试。"),
            true);
    }
}

void AppController::onConnectionError(const QString &message)
{
    m_isConnecting = false;

    // 一旦收到连接错误，就结束当前认证尝试并清理队列。
    // 某些 socket 错误发出时状态可能还短暂显示为 Connected，
    // 因此这里不依赖 isConnected() 的瞬时值，避免请求一直卡在处理中。
    m_pendingRequests.clear();
    m_requestsAwaitingResponse.clear();
    m_authRequestPending = false;
    m_authRequestTimer.stop();

    showResponseMessage(
        QStringLiteral("服务器连接失败"),
        message + QStringLiteral("\n\n请检查 client.conf 中的 SERVER_HOST 和 SERVER_PORT，以及服务端是否已启动。"),
        true);
}

void AppController::sendPendingRequests()
{
    // 只要仍然连接着服务器、队列中也还有请求，就继续逐个发送。
    // 每次 dequeue 都会移除队首请求，保证不会被重复发送。
    while (m_tcpClient->isConnected() && !m_pendingRequests.isEmpty())
    {
        const PendingRequest request = m_pendingRequests.dequeue();
        if (!sendRequest(request))
        {
            // 发送失败时停止继续发送，避免在连接异常时继续处理队列。
            break;
        }
    }
}

void AppController::onPacketReceived(quint16 command,
                                     quint64 requestId,
                                     const QByteArray &jsonBody)
{
    // 服务器响应头应带回原请求的 requestId。只有能找到对应请求时，
    // 才把它当作本次登录/注册的响应处理。requestId 为 0 的主动推送，
    // 会在后续聊天功能阶段单独处理；当前认证阶段先不消费这类消息。
    auto requestIt = m_requestsAwaitingResponse.find(requestId);
    if (requestIt == m_requestsAwaitingResponse.end())
    {
        return;
    }

    const protocolHeader::CommandType originalCommand = requestIt.value();
    m_requestsAwaitingResponse.erase(requestIt);
    m_authRequestPending = false;
    m_authRequestTimer.stop();

    // TcpClient 将协议头中的 command 以 quint16 传上来，这里转回枚举方便判断。
    const auto responseCommand = static_cast<protocolHeader::CommandType>(command);

    // 验证响应是否属于对应请求，避免把错误类型的响应当成登录/注册结果。
    const bool isLoginResponse =
        originalCommand == protocolHeader::CommandType::LOGIN_REQUEST &&
        (responseCommand == protocolHeader::CommandType::LOGIN_SUCCESS ||
         responseCommand == protocolHeader::CommandType::LOGIN_FAILED);
    const bool isRegisterResponse =
        originalCommand == protocolHeader::CommandType::REGISTER_REQUEST &&
        (responseCommand == protocolHeader::CommandType::REGISTER_SUCCESS ||
         responseCommand == protocolHeader::CommandType::REGISTER_FAILED);

    if (!isLoginResponse && !isRegisterResponse)
    {
        showResponseMessage(
            QStringLiteral("服务器响应异常"),
            QStringLiteral("收到的响应命令与当前请求不匹配，请检查客户端与服务端协议版本。"),
            true);
        return;
    }

    // 先解析服务器返回的 JSON。若格式不正确，后续就不能安全读取字段。
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBody, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        showResponseMessage(
            QStringLiteral("服务器响应格式错误"),
            QStringLiteral("无法解析服务器返回的 JSON。"), true);
        return;
    }

    const QJsonObject body = document.object();

    // 从响应 JSON 中读取 message 字段；如果服务器未提供，则显示默认提示。
    const QString message = body.value(QString::fromLatin1(protocolBody::MESSAGE))
                                .toString(QStringLiteral("服务器已返回响应。"));

    switch (responseCommand)
    {
    case protocolHeader::CommandType::LOGIN_SUCCESS:
        // 服务器明确返回登录成功后，才通知 LoginWindow 发出 loginSucceeded。
        // 不要在用户点击登录时就切换窗口，因为服务器可能拒绝账号或密码。
        m_loginWindow->notifyLoginSucceeded();
        break;

    case protocolHeader::CommandType::LOGIN_FAILED:
        showResponseMessage(QStringLiteral("登录失败"), message, true);
        break;

    case protocolHeader::CommandType::REGISTER_SUCCESS:
        // 注册成功只提示用户，不自动登录；用户可以再回到登录页进行登录。
        showResponseMessage(QStringLiteral("注册成功"), message, false);
        break;

    case protocolHeader::CommandType::REGISTER_FAILED:
        showResponseMessage(QStringLiteral("注册失败"), message, true);
        break;

    default:
        // AppController 当前只处理登录/注册响应，聊天等其他命令留给后续业务控制器。
        break;
    }
}

void AppController::onLoginSucceeded()
{
    // 如果主窗口尚未创建，就创建一次；重复收到信号也不会反复创建新窗口。
    if (m_mainWindow == nullptr)
    {
        m_mainWindow = new MainWindow;
    }

    // 显示并激活主窗口，确保它出现在前台。
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();

    // 登录成功后隐藏登录窗口而不是立刻销毁它，便于控制整个应用中的顶层窗口生命周期。
    if (m_loginWindow != nullptr)
    {
        m_loginWindow->hide();
    }
}

void AppController::showResponseMessage(const QString &title,
                                        const QString &message,
                                        bool isError)
{
    // 优先把消息框附着在当前可见的窗口上，避免消息框出现在不相关的窗口后面。
    QWidget *parentWidget = m_loginWindow;
    if (m_mainWindow != nullptr && m_mainWindow->isVisible())
    {
        parentWidget = m_mainWindow;
    }

    // 错误提示使用 warning，普通结果提示使用 information。
    if (isError)
    {
        QMessageBox::warning(parentWidget, title, message);
    }
    else
    {
        QMessageBox::information(parentWidget, title, message);
    }
}
