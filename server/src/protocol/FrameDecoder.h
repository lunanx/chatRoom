#pragma once

#include<string>
#include"protocol/ProtocolHeader.h"

class FrameDecoder
{
public:
    FrameDecoder();
    /*
        解析客户端数据包
        返回值: -1 表示数据有错误
                0 表示数据不够
                1 表示是完整的一份数据 
    */
    int parseBufPacket(std::string& bufPacket);
    /*
        封装服务器数据包
        参数就是自己的成员变量，用完后最好再初始化
        这个先不急，先按照路线一步步来
    */
    std::string buildServerPacket();

private:
    protocolHeader::CommandType m_command; // 接收一个完整的数据包请求类型
    std::string m_body;// 接收一个完整的数据包的数据
    std::uint64_t m_requestId; // 接收一个完整的数据包的id
};