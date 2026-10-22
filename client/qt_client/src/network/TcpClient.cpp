#include "TcpClient.h"

#include "ProtocolCodec.h"
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QtGlobal>

// 创建 TCP Socket，并绑定相关事件
TcpClient::TcpClient(QObject *parent)
    : QObject(parent),
      m_socket(new QTcpSocket(this))
{

    // 连接本地虚拟网络中的服务器时，不使用系统代理
    m_socket->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));

    // TCP 连接成功时，通知上层
    connect(
        m_socket,
        &QTcpSocket::connected,
        this,
        &TcpClient::connectedToServer);

    // TCP 连接断开时，通知上层
    connect(
        m_socket,
        &QTcpSocket::disconnected,
        this,
        &TcpClient::disconnectedFromServer);

    // 有数据到达时，读取并解析
    connect(
        m_socket,
        &QTcpSocket::readyRead,
        this,
        &TcpClient::onReadyRead);

    // 发生 TCP 错误时，通知上层
    connect(
        m_socket,
        &QTcpSocket::errorOccurred,
        this,
        &TcpClient::onSocketError);
}

// 异步连接服务器
void TcpClient::connectToServer(const QString &host, quint16 port)
{
    if (host.trimmed().isEmpty() || port == 0)
    {
        emit connectionError("服务器地址或端口无效");
        return;
    }

    // 不能让上一次连接遗留的数据混入新连接
    m_receiveBuffer.clear();

    // 如果当前还处于连接中或已连接状态，先结束旧连接
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
    {
        m_socket->abort();
    }

    m_socket->connectToHost(host, port);
}

// 请求断开 TCP 连接
void TcpClient::disconnectFromServer()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
    {
        m_socket->disconnectFromHost();
    }
}

// 判断连接状态
bool TcpClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

// 将 JSON 请求编码为完整数据包后发送
bool TcpClient::sendRequest(
    protocolHeader::CommandType command,
    quint64 requestId,
    const QJsonObject &body)
{
    if (!isConnected())
    {
        emit connectionError("尚未连接服务器，无法发送请求");
        return false;
    }

    // 将 JSON 对象转换为紧凑格式的 UTF-8 字节
    const QByteArray jsonBody =
        QJsonDocument(body).toJson(QJsonDocument::Compact);

    // 使用已有的协议编码器生成数据包
    const QByteArray packet = ProtocolCodec::encode(
        command,
        static_cast<std::uint64_t>(requestId),
        jsonBody);

    if (packet.isEmpty())
    {
        emit protocolError("请求数据包编码失败");
        return false;
    }

    // QTcpSocket 会管理待发送的数据
    if (m_socket->write(packet) < 0)
    {
        emit connectionError(m_socket->errorString());
        return false;
    }

    return true;
}

// 读取并解析服务器返回的数据
void TcpClient::onReadyRead()
{
    // 新数据先追加到缓冲区，不能假设一次收到完整数据包
    m_receiveBuffer.append(m_socket->readAll());

    // 一个缓冲区中可能包含多个完整数据包
    while (!m_receiveBuffer.isEmpty())
    {
        protocolHeader::PacketFrame frame{};
        QString error;

        const auto status = ProtocolCodec::tryDecode(
            m_receiveBuffer,
            frame,
            error);

        if (status == ProtocolCodec::DecodeStatus::NeedMoreData)
        {
            // 数据尚未收全，等待下次 readyRead
            return;
        }

        if (status == ProtocolCodec::DecodeStatus::ProtocolError)
        {
            // 协议错误时，不再尝试解析当前缓冲区
            emit protocolError(error);
            m_receiveBuffer.clear();
            m_socket->abort();
            return;
        }

        // 将服务端的 std::string 消息体转换为 Qt 字节数组
        const QByteArray jsonBody(
            frame.m_body.data(),
            static_cast<qsizetype>(frame.m_body.size()));

        emit packetReceived(
            static_cast<quint16>(frame.m_command),
            static_cast<quint64>(frame.m_requestId),
            jsonBody);
    }
}

// 处理 TCP Socket 错误
void TcpClient::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);

    emit connectionError(m_socket->errorString());
}