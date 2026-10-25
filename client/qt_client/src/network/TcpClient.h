#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include "protocol/ProtocolHeader.h"

// Qt 客户端的 TCP 通信类
class TcpClient : public QObject
{
    Q_OBJECT

public:
    explicit TcpClient(QObject *parent = nullptr);

    // 连接服务器，连接过程是异步的
    void connectToServer(const QString &host, quint16 port);

    // 请求正常断开 TCP 连接；允许 Qt 尝试完成待发送数据。
    void disconnectFromServer();

    // 立即中止当前连接并清空接收缓冲区。
    // 用于响应超时或会话状态不确定的情况，避免复用可能已认证的旧连接。
    void abortConnection();

    // 判断当前是否已经建立连接
    bool isConnected() const;

    // 发送一个 JSON 请求
    bool sendRequest(
        protocolHeader::CommandType command,
        quint64 requestId,
        const QJsonObject &body);

signals:
    // 连接建立成功
    void connectedToServer();

    // 连接断开
    void disconnectedFromServer();

    // TCP 连接错误
    void connectionError(const QString &message);

    // 成功收到一个完整的数据包
    void packetReceived(
        quint16 command,
        quint64 requestId,
        const QByteArray &jsonBody);

    // 收到不符合协议的数据
    void protocolError(const QString &message);

private slots:
    // QTcpSocket 收到新数据时调用
    void onReadyRead();

    // QTcpSocket 发生错误时调用
    void onSocketError(QAbstractSocket::SocketError error);

private:
    // 负责底层 TCP 连接与收发
    QTcpSocket *m_socket = nullptr;

    // 保存尚未解析完的数据
    QByteArray m_receiveBuffer;
};