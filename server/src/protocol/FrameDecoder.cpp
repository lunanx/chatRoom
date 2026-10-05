#include "FrameDecoder.h"
#include <string.h>
#include <arpa/inet.h>
#include <cstdint>
#include <endian.h>

DecoderStatus FrameDecoder::parseBufPacket(std::string &bufPacket, struct Packet &outputFrame)
{
    if (bufPacket.size() >= protocolHeader::HEADER_SIZE)
    {
        // 读取Header
        protocolHeader::Header header;
        memcpy(&header, bufPacket.data(), protocolHeader::HEADER_SIZE);
        // 检查magic
        if (ntohl(header.magic) != protocolHeader::MAGIC)
        {
            return DecoderStatus::ProtocolError;
        }
        // 检查version
        if (ntohs(header.version) != static_cast<std::uint16_t>(1))
        {
            return DecoderStatus::ProtocolError;
        }
        // 检查body_length
        std::uint32_t bodyLength = ntohl(header.body_length);
        if (bodyLength > protocolHeader::MAX_BODY_LENGTH)
        {
            return DecoderStatus::ProtocolError;
        }
        // 检查HEADER_SIZE + body_length 是否已经全部收到
        if (bufPacket.size() < bodyLength + protocolHeader::HEADER_SIZE)
        {
            return DecoderStatus::NeedMoreData;
        }
        // 得到一个完整的Frame,其实只需要有 command、request_id 和 body
        outputFrame.m_command = static_cast<protocolHeader::CommandType>(ntohs(header.command));
        outputFrame.m_requestId = be64toh(header.request_id);
        outputFrame.m_body = bufPacket.substr(protocolHeader::HEADER_SIZE,bodyLength);
        // 从缓冲区删除这个Frame
        bufPacket.erase(0, protocolHeader::HEADER_SIZE + bodyLength);
        return DecoderStatus::PacketReady;
    }

    return DecoderStatus::NeedMoreData;
}
