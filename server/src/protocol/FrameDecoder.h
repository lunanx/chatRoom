#pragma once

#include <string>
#include "protocol/ProtocolHeader.h"

enum class DecoderStatus
{
    NeedMoreData,  // 数据不够
    ProtocolError, // 数据有错误
    PacketReady    // 是一个完整的数据包
};


/*
    将客户端数据包解析
    此类设置为无成员变量，避免新Frame覆盖旧Frame
*/
class FrameDecoder
{
public:
    /*
        解析客户端数据包
        返回值: ProtocolError 表示数据有错误,上层直接关闭客户端
                NeedMoreData 表示数据不够，上层重新recv
                PacketReady 表示是完整的一份数据,上层处理完整的Frame
    */
    DecoderStatus parseBufPacket(const std::string &bufPacket, protocolHeader::PacketFrame &outputFrame, size_t &offset);
};