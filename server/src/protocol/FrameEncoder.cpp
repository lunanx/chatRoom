#include "FrameEncoder.h"
#include <arpa/inet.h>

std::string FrameEncoder::buildBufPacket(
    std::uint16_t m_command,
    std::uint64_t m_requestId,
    const nlohmann::json &body)
{
    //将json转换成string
    std::string bodyStr = body.dump();
    //创建Header
    protocolHeader::Header header;
    //设置Header变量
    header.magic = htonl(protocolHeader::MAGIC);
    header.command = htons(m_command);
    header.version = htons(protocolHeader::VERSION);
    header.request_id = htobe64(m_requestId);
    header.body_length = htonl(bodyStr.size());
    //打包header + body
    std::string packet;
    packet.reserve(protocolHeader::HEADER_SIZE + bodyStr.size());
    //一定是先加header，再加body
    packet.append(reinterpret_cast<const char*>(&header),protocolHeader::HEADER_SIZE);
    packet.append(bodyStr);

    return packet;
}
