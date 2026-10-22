#include "network/ProtocolCodec.h"

#include <QtEndian>
#include <cstdint>
#include <iostream>
#include <string>

// 测试 ProtocolCodec::encode() 是否生成正确的数据包
int main()
{
    // 准备一个 JSON 消息体
    const QByteArray jsonBody =
        R"({"username":"test_user","password":"test_password"})";

    // 使用一个容易辨认的请求编号，方便检查字节序
    const std::uint64_t requestId = 0x0102030405060708ULL;

    // 生成登录请求数据包
    const QByteArray packet = ProtocolCodec::encode(
        protocolHeader::CommandType::LOGIN_REQUEST,
        requestId,
        jsonBody);

    // 编码失败时不应继续读取数据包
    if (packet.isEmpty())
    {
        std::cerr << "FAIL: encode returned an empty packet\n";
        return 1;
    }

    // 将数据包转换为只读字节指针，便于读取各字段
    const auto *data =
        reinterpret_cast<const uchar *>(packet.constData());

    const qsizetype headerSize =
        static_cast<qsizetype>(protocolHeader::HEADER_SIZE);

    // 检查协议头长度
    if (headerSize != 20)
    {
        std::cerr << "FAIL: header size is not 20\n";
        return 1;
    }

    // 检查完整数据包长度
    if (packet.size() != headerSize + jsonBody.size())
    {
        std::cerr << "FAIL: packet size is incorrect\n";
        return 1;
    }

    // 检查魔数
    if (qFromBigEndian<quint32>(data) != protocolHeader::MAGIC)
    {
        std::cerr << "FAIL: magic is incorrect\n";
        return 1;
    }

    // 检查协议版本
    if (qFromBigEndian<quint16>(data + 4) !=
        protocolHeader::VERSION)
    {
        std::cerr << "FAIL: version is incorrect\n";
        return 1;
    }

    // 检查命令编号
    const quint16 command =
        qFromBigEndian<quint16>(data + 6);

    if (command != static_cast<quint16>(
                       protocolHeader::CommandType::LOGIN_REQUEST))
    {
        std::cerr << "FAIL: command is incorrect\n";
        return 1;
    }

    // 检查消息体长度
    const quint32 bodyLength =
        qFromBigEndian<quint32>(data + 8);

    if (bodyLength != static_cast<quint32>(jsonBody.size()))
    {
        std::cerr << "FAIL: body length is incorrect\n";
        return 1;
    }

    // 检查请求编号
    const quint64 decodedRequestId =
        qFromBigEndian<quint64>(data + 12);

    if (decodedRequestId != requestId)
    {
        std::cerr << "FAIL: request id is incorrect\n";
        return 1;
    }

    // 检查协议头后面的数据是否与原始 JSON 完全一致
    if (packet.mid(headerSize) != jsonBody)
    {
        std::cerr << "FAIL: JSON body is incorrect\n";
        return 1;
    }

    // ==================== 解码测试 ====================

    QString error;
    protocolHeader::PacketFrame decodedFrame;

    // 测试一：只收到部分协议头
    QByteArray incompleteHeader = packet.left(10);
    const QByteArray originalHeader = incompleteHeader;

    const auto headerStatus = ProtocolCodec::tryDecode(
        incompleteHeader, decodedFrame, error);

    if (headerStatus != ProtocolCodec::DecodeStatus::NeedMoreData ||
        incompleteHeader != originalHeader)
    {
        std::cerr << "FAIL: incomplete header handling is incorrect\n";
        return 1;
    }

    // 测试二：协议头完整，但 JSON 消息体还未收完
    const qsizetype partialLength = headerSize + 3;

    QByteArray incompleteBody = packet.left(partialLength);
    const QByteArray originalBody = incompleteBody;

    const auto bodyStatus = ProtocolCodec::tryDecode(
        incompleteBody, decodedFrame, error);

    if (bodyStatus != ProtocolCodec::DecodeStatus::NeedMoreData ||
        incompleteBody != originalBody)
    {
        std::cerr << "FAIL: incomplete body handling is incorrect\n";
        return 1;
    }

    // 将剩余字节追加到缓冲区，重新尝试解析
    incompleteBody.append(packet.mid(partialLength));

    const auto completeStatus = ProtocolCodec::tryDecode(
        incompleteBody, decodedFrame, error);

    if (completeStatus != ProtocolCodec::DecodeStatus::PacketReady)
    {
        std::cerr << "FAIL: complete packet was not decoded\n";
        return 1;
    }

    // 检查解码出的命令、请求编号和 JSON 消息体
    const auto expectedCommand = static_cast<quint16>(
        protocolHeader::CommandType::LOGIN_REQUEST);

    const std::string expectedBody(
        jsonBody.constData(),
        static_cast<std::size_t>(jsonBody.size()));

    if (decodedFrame.m_command != expectedCommand ||
        decodedFrame.m_requestId != requestId ||
        decodedFrame.m_body != expectedBody)
    {
        std::cerr << "FAIL: decoded packet fields are incorrect\n";
        return 1;
    }

    // 成功解码后，当前缓冲区应该不再包含已处理的数据
    if (!incompleteBody.isEmpty())
    {
        std::cerr << "FAIL: decoded bytes were not removed\n";
        return 1;
    }

    // 测试三：两个完整数据包连续到达（模拟 TCP 粘包）
    QByteArray stickyBuffer = packet;
    stickyBuffer.append(packet);

    const auto firstStatus = ProtocolCodec::tryDecode(
        stickyBuffer, decodedFrame, error);

    if (firstStatus != ProtocolCodec::DecodeStatus::PacketReady ||
        stickyBuffer != packet)
    {
        std::cerr << "FAIL: first packet in sticky buffer is incorrect\n";
        return 1;
    }

    const auto secondStatus = ProtocolCodec::tryDecode(
        stickyBuffer, decodedFrame, error);

    if (secondStatus != ProtocolCodec::DecodeStatus::PacketReady ||
        !stickyBuffer.isEmpty())
    {
        std::cerr << "FAIL: second packet in sticky buffer is incorrect\n";
        return 1;
    }

    // 测试四：错误魔数
    QByteArray badMagic = packet;
    badMagic[0] = '\0';

    const auto magicStatus = ProtocolCodec::tryDecode(
        badMagic, decodedFrame, error);

    if (magicStatus != ProtocolCodec::DecodeStatus::ProtocolError)
    {
        std::cerr << "FAIL: invalid magic was not rejected\n";
        return 1;
    }

    // 测试五：消息体长度超过协议限制
    QByteArray oversizedPacket = packet.left(headerSize);

    qToBigEndian<quint32>(
        static_cast<quint32>(protocolHeader::MAX_BODY_LENGTH + 1),
        oversizedPacket.data() + 8);

    const auto oversizedStatus = ProtocolCodec::tryDecode(
        oversizedPacket, decodedFrame, error);

    if (oversizedStatus != ProtocolCodec::DecodeStatus::ProtocolError)
    {
        std::cerr << "FAIL: oversized body length was not rejected\n";
        return 1;
    }

    std::cout << "PASS: all encode/decode checks passed\n";
    return 0;
}