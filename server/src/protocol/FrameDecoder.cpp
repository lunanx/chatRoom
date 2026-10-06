#include "FrameDecoder.h"
#include <string.h>
#include <arpa/inet.h>
#include <cstdint>
#include <endian.h>

DecoderStatus FrameDecoder::parseBufPacket(const std::string &bufPacket, struct DecodedFrame &outputFrame, size_t &offset)
{
    // 注意data和size要offset偏移，bufPacket在erase前都还是原本的
    size_t available = bufPacket.size() - offset;
    if (available >= protocolHeader::HEADER_SIZE)
    {
        // 读取Header
        protocolHeader::Header header;
        memcpy(&header, bufPacket.data() + offset, protocolHeader::HEADER_SIZE);
        // 检查magic
        if (ntohl(header.magic) != protocolHeader::MAGIC)
        {
            printf("magic 不对\n");
            return DecoderStatus::ProtocolError;
        }
        // 检查version
        if (ntohs(header.version) != protocolHeader::VERSION)
        {
            printf("version 不对\n");
            return DecoderStatus::ProtocolError;
        }
        // 检查body_length
        std::uint32_t bodyLength = ntohl(header.body_length);
        if (bodyLength > protocolHeader::MAX_BODY_LENGTH)
        {
            printf("bodyLength 太长\n");
            return DecoderStatus::ProtocolError;
        }
        // 检查HEADER_SIZE + body_length 是否已经全部收到
        if (available < bodyLength + protocolHeader::HEADER_SIZE)
        {
            return DecoderStatus::NeedMoreData;
        }
        // 得到一个完整的Frame,其实只需要有 command、request_id 和 body
        outputFrame.m_command = static_cast<std::uint16_t>(ntohs(header.command));
        outputFrame.m_requestId = be64toh(header.request_id);
        outputFrame.m_body = bufPacket.substr(offset + protocolHeader::HEADER_SIZE, bodyLength);
        // 修改offset
        offset += protocolHeader::HEADER_SIZE + bodyLength;

        return DecoderStatus::PacketReady;
    }

    return DecoderStatus::NeedMoreData;
}
