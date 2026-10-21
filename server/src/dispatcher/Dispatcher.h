#pragma once

#include "protocol/ProtocolHeader.h"
#include "service/UserService.h"
#include "service/MessageService.h"
#include "service/ConversationService.h"
#include "service/FriendService.h"
#include "net/ClientSession.h"
#include <optional>

struct DispatchResult
{
    protocolHeader::PacketFrame frame;
    std::optional<Message> message; // 不需要广播时就为空
    std::optional<std::uint64_t> conversationId;// 当会话创建成功时需要给客户端返回
    std::optional<std::uint64_t> lastSeq; // 对应数据库中的 Conversation.last_seq
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
