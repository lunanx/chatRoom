#include "FrameDecoder.h"
#include <string.h>
#include <arpa/inet.h>
#include <cstdint>
#include <endian.h>

FrameDecoder::FrameDecoder()
    : m_command(protocolHeader::CommandType::UNKNOWN),
      m_requestId(static_cast<std::uint64_t>(1))
{
    m_body.clear();
}

int FrameDecoder::parseBufPacket(std::string &bufPacket)
{
    if (bufPacket.size() >= protocolHeader::HEADER_SIZE)
    {
        // 读取Header
        protocolHeader::Header header;
        memcpy(&header, bufPacket.data(), protocolHeader::HEADER_SIZE);
        // 检查magic
        if (ntohl(header.magic) != protocolHeader::MAGIC)
        {
            return -1;
        }
        // 检查version
        if (ntohs(header.version) != static_cast<std::uint16_t>(1))
        {
            return -1;
        }
        // 检查body_length
        std::uint32_t bodyLength = ntohl(header.body_length);
        if (bodyLength > protocolHeader::MAX_BODY_LENGTH)
        {
            return -1;
        }
        // 检查HEADER_SIZE + body_length 是否已经全部收到
        if (bufPacket.size() < bodyLength + protocolHeader::HEADER_SIZE)
        {
            return 0;
        }
        // 得到一个完整的Frame,其实只需要有 command、request_id 和 body
        m_command = static_cast<protocolHeader::CommandType>(ntohs(header.command));
        m_requestId = be64toh(header.request_id);
        //有个疑问，这里不需要转字节序吗？
        m_body = bufPacket.substr(protocolHeader::HEADER_SIZE,bodyLength);
        // 从缓冲区删除这个Frame
        bufPacket.erase(0,protocolHeader::HEADER_SIZE + bodyLength);
        
        return 1;
    }

    return 0;
}
