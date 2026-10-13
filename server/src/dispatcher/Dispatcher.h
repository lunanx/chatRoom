#pragma once

#include "protocol/ProtocolHeader.h"
#include "service/UserService.h"
#include "service/MessageService.h"
#include "net/ClientSession.h"
#include <optional>

struct DispatchResult
{
    protocolHeader::PacketFrame frame;
    std::optional<Message> message; // 不需要广播时就为空
};
/*
    业务调度器
*/
class Dispatcher
{
public:
    Dispatcher() = default;
    /*
        处理当前连接发出的请求
        ClientSession由SubReactor所有,Dispatcher只借用它
    */
    DispatchResult dispatch(const protocolHeader::PacketFrame &frame, ClientSession &session);
};
