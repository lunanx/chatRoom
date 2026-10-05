#pragma once

#include <string>
#include "protocol/ProtocolHeader.h"
#include "DecodedFrame.h"

enum class DecoderStatus
{
    NeedMoreData,  // 继续recv
    ProtocolError, // 直接关闭客户端
    PacketReady    // 是一个完整的数据包
};


class FrameDecoder
{
public:
    /*
        解析客户端数据包
        返回值: -1 表示数据有错误
                0 表示数据不够
                1 表示是完整的一份数据
    */
    DecoderStatus parseBufPacket(std::string &bufPacket,struct Packet& outputFrame);
    /*
        封装服务器数据包
        参数就是自己的成员变量，用完后最好再初始化
        这个先不急，先按照路线一步步来
    */
    std::string buildServerPacket();
};