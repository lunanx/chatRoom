#pragma once

#include "protocol/ProtocolHeader.h"
#include "service/UserService.h"
#include "net/ClientSession.h"
class Dispatcher
{
public:
    Dispatcher() = default;
    /*
        处理当前连接发出的请求
        ClientSession由SubReactor所有,Dispatcher只借用它
    */
    protocolHeader::PacketFrame dispatch(const protocolHeader::PacketFrame &frame,ClientSession& session);
};
