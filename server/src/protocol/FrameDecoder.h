#pragma once

#include <string>
#include "protocol/ProtocolHeader.h"

enum class DecoderStatus
{
    NeedMoreData,  // 数据不够
    ProtocolError, // 数据有错误
    PacketReady    // 是一个完整的数据包
};

// 相当于解析出来的数据包，这里不能用作FrameDecoder的变量，不然会存在，A还没解析，B的数据被解析出来覆盖了A
struct DecodedFrame
{
    protocolHeader::CommandType m_command;
    std::string m_body;
    std::uint64_t m_requestId;
};


// 将FrameDecoder设置为无状态，只用作解析数据。
class FrameDecoder
{
public:
    /*
        解析客户端数据包
        返回值: ProtocolError 表示数据有错误,上层直接关闭客户端
                NeedMoreData 表示数据不够，上层重新recv
                PacketReady 表示是完整的一份数据,上层处理完整的Frame
    */
    DecoderStatus parseBufPacket(std::string &bufPacket,struct DecodedFrame& outputFrame);
    // /*
    //     封装服务器数据包
    //     参数就是自己的成员变量，用完后最好再初始化
    //     这个先不急，先按照路线一步步来
    // */
    // std::string buildServerPacket();
};