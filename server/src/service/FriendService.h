#pragma once

#include "protocol/ProtocolHeader.h"
#include <cstdint>
#include <string>

class FriendService
{
public:
    /*
        好友请求业务
        body: {"target_username":"..."}; authenticatedUserId 必须来自 ClientSession。
    */
    static protocolHeader::CommandType sendFriendRequest(
        const std::string &body,
        std::string &responseBody,
        std::uint64_t authenticatedUserId);

    /*
        查询当前登录用户收到的、尚未处理的好友申请业务
    */
    static protocolHeader::CommandType listFriendRequests(
        std::string &responseBody,
        std::uint64_t authenticatedUserId);
    /*
        处理好友请求业务
        body: {"friend_request_id":123,"action":1|2}; 1=同意，2=拒绝。
    */
    static protocolHeader::CommandType handleFriendRequest(
        const std::string &body,
        std::string &responseBody,
        std::uint64_t authenticatedUserId);

    /*
        查询好友列表业务
    */
    static protocolHeader::CommandType listFriends(
        std::string &responseBody,
        std::uint64_t authenticatedUserId);
};
