#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>

// 直接使用项目已有的公共协议定义
#include "protocol/ProtocolHeader.h"

// 负责 Qt 客户端的数据包编码和解码
class ProtocolCodec
{
public:
    // 解码结果
    enum class DecodeStatus
    {
        NeedMoreData,  // 数据还不完整，需要继续接收
        PacketReady,   // 成功解析出一个完整数据包
        ProtocolError  // 收到的数据不符合协议
    };

    // 将命令、请求编号和 JSON 字节数据编码成完整数据包
    static QByteArray encode(
        protocolHeader::CommandType command,
        std::uint64_t requestId,
        const QByteArray &jsonBody);

    // 尝试从接收缓冲区解析一个完整数据包
    static DecodeStatus tryDecode(
        QByteArray &buffer,
        protocolHeader::PacketFrame &outputFrame,
        QString &error);
};
