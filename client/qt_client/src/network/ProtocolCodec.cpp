#include "ProtocolCodec.h"

#include <QtEndian>

// 将命令、请求编号和 JSON 字节编码成一个完整数据包
QByteArray ProtocolCodec::encode(
    protocolHeader::CommandType command,
    std::uint64_t requestId,
    const QByteArray &jsonBody)
{
    // 检查消息体是否超过协议允许的最大长度
    if (jsonBody.size() >
        static_cast<qsizetype>(protocolHeader::MAX_BODY_LENGTH))
    {
        return {};
    }

    // 创建固定长度的协议头，初始内容全部为 0
    QByteArray header(
        static_cast<qsizetype>(protocolHeader::HEADER_SIZE), '\0');

    // 获取协议头的各字段在字节数组中的起始位置
    char *data = header.data();

    // 按网络字节序写入协议头
    // 偏移 0：magic，占 4 字节
    qToBigEndian<quint32>(
        protocolHeader::MAGIC, data);

    // 偏移 4：version，占 2 字节
    qToBigEndian<quint16>(
        protocolHeader::VERSION, data + 4);

    // 偏移 6：command，占 2 字节
    qToBigEndian<quint16>(
        static_cast<quint16>(command), data + 6);

    // 偏移 8：body_length，占 4 字节
    qToBigEndian<quint32>(
        static_cast<quint32>(jsonBody.size()), data + 8);

    // 偏移 12：request_id，占 8 字节
    qToBigEndian<quint64>(
        static_cast<quint64>(requestId), data + 12);

    // 完整数据包 = 20 字节协议头 + JSON 消息体
    QByteArray packet;
    packet.reserve(header.size() + jsonBody.size());

    packet.append(header);
    packet.append(jsonBody);

    return packet;
}

// 尝试从接收缓冲区解析一个完整数据包
ProtocolCodec::DecodeStatus ProtocolCodec::tryDecode(
    QByteArray &buffer,
    protocolHeader::PacketFrame &outputFrame,
    QString &error)
{
    // 清除上一次的错误信息
    error.clear();

    // 获取协议头长度
    const qsizetype headerSize =
        static_cast<qsizetype>(protocolHeader::HEADER_SIZE);

    // 第一步：检查是否已经收到完整的协议头
    if (buffer.size() < headerSize)
    {
        return DecodeStatus::NeedMoreData;
    }

    // 将接收缓冲区视为只读字节数组
    const auto *data =
        reinterpret_cast<const uchar *>(buffer.constData());

    // 第二步：检查协议魔数
    const quint32 magic = qFromBigEndian<quint32>(data);

    if (magic != protocolHeader::MAGIC)
    {
        error = "协议魔数错误";
        return DecodeStatus::ProtocolError;
    }

    // 第三步：检查协议版本
    const quint16 version =
        qFromBigEndian<quint16>(data + 4);

    if (version != protocolHeader::VERSION)
    {
        error = "协议版本不受支持";
        return DecodeStatus::ProtocolError;
    }

    // 第四步：读取 JSON 消息体长度
    const quint32 bodyLength =
        qFromBigEndian<quint32>(data + 8);

    // 防止异常数据声明一个过大的消息体
    if (bodyLength > protocolHeader::MAX_BODY_LENGTH)
    {
        error = "消息体长度超过协议限制";
        return DecodeStatus::ProtocolError;
    }

    // 计算完整数据包的长度
    const qsizetype totalLength =
        headerSize + static_cast<qsizetype>(bodyLength);

    // 第五步：检查是否已经收到完整的数据包
    if (buffer.size() < totalLength)
    {
        return DecodeStatus::NeedMoreData;
    }

    // 第六步：提取命令编号
    outputFrame.m_command =
        qFromBigEndian<quint16>(data + 6);

    // 第七步：提取请求编号
    outputFrame.m_requestId =
        qFromBigEndian<quint64>(data + 12);

    // 第八步：提取 JSON 消息体
    // 使用明确的长度构造 string，不能依赖字符串结束符 '\0'
    outputFrame.m_body = std::string(
        buffer.constData() + headerSize,
        static_cast<std::size_t>(bodyLength));

    // 第九步：删除已经处理的数据包
    // 如果后面还有其他数据包，会保留在 buffer 中
    buffer.remove(0, totalLength);

    return DecodeStatus::PacketReady;
}