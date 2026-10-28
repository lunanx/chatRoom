#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstddef>
#include <string>

/*
   =============================
   客户端 / 服务器公共通信协议
   =============================

   数据包结构:
   ----------Header------------------
    magic         4 bytes
    version       2 bytes
    command       2 bytes
    body_length   4 bytes
    request_id    8 bytes
    ----------Body-------------------
        JSON UTF-8
    ---------------------------------

    Header总长度: 20 bytes

    整数用网络字节序---通过主机转网络
    --------------------------------------------
    对于request_id:
        客户端主动请求：
            request_id = 客户端生成的唯一请求编号

            服务器回复：
            request_id = 原请求的 request_id

            服务器主动推送：
            request_id = 0

*/
namespace protocolHeader
{
    /*
        协议魔数

        用于判断当前解析的TCP数据是否符合我们的协议的开头
    */
    constexpr std::uint32_t MAGIC = 0x12345678;

    /*
        请求和相应类型,服务器和客户端需要保持一致
    */
    enum class CommandType : std::uint16_t
    {
        UNKNOWN = 0,

        /*
            登录
        */
        LOGIN_REQUEST = 1,
        LOGIN_SUCCESS = 2,
        LOGIN_FAILED = 3,

        /*
            退出登录
        */
        LOGOUT_REQUEST = 4,
        LOGOUT_SUCCESS = 5,
        LOGOUT_FAILED = 6,

        /*
            注册
        */
        REGISTER_REQUEST = 7,
        REGISTER_SUCCESS = 8,
        REGISTER_FAILED = 9,

        /*
            群聊消息
        */
        GROUP_MESSAGE_SEND = 10,
        GROUP_MESSAGE_RECV = 11,

        /*
            私聊消息
        */
        PRIVATE_MESSAGE_SEND = 12,
        PRIVATE_MESSAGE_RECV = 13,

        /*
            用户上线 / 下线
        */
        USER_JOIN = 14,
        USER_LEAVE = 15,

        /*
            消息发送状态
        */
        MESSAGE_SEND_FAILED = 16,
        MESSAGE_SEND_SUCCESS = 17,

        /*
            创建会话通用
        */
        CHATCREATE_REQUEST = 18,
        /*
            群聊
        */
        GROUPCREATE_SUCCESS = 19,
        GROUPCREATE_FAILED = 20,

        /*
            私聊
        */
        PRIVATECREATE_SUCCESS = 21,
        PRIVATECREATE_FAILED = 22,

        /*
            历史消息请求
        */
        MESSAGE_HISTORY_REQUEST = 23,
        MESSAGE_HISTORY_SUCCESS = 24,
        MESSAGE_HISTORY_FAILED = 25,

        /*
            好友申请发送-- A->B
        */
        FRIEND_REQUEST_SEND = 26,
        FRIEND_REQUEST_SEND_SUCCESS = 27,
        FRIEND_REQUEST_SEND_FAILED = 28,

        /*
            查询当前用户收到的待处理好友申请-- B->A
        */
        QUERY_PENDING_FRIEND_LIST_REQUEST = 29,
        QUERY_PENDING_FRIEND_LIST_SUCCESS = 30,
        QUERY_PENDING_FRIEND_LIST_FAILED = 31,

        /*
            同意或拒绝好友申请-- B-->A
        */
        FRIEND_REQUEST_HANDLE_REQUEST = 32,
        FRIEND_REQUEST_HANDLE_SUCCESS = 33,
        FRIEND_REQUEST_HANDLE_FAILED = 34,

        /*
            查询当前用户的好友列表
        */
        QUERY_FRIEND_LIST_REQUEST = 35,
        QUERY_FRIEND_LIST_SUCCESS = 36,
        QUERY_FRIEND_LIST_FAILED = 37,

        /*
            查询当前登录用户的会话列表
        */
        QUERY_CONVERSATION_LIST_REQUEST = 38,
        QUERY_CONVERSATION_LIST_SUCCESS = 39,
        QUERY_CONVERSATION_LIST_FAILED = 40,
        
        /*
            邀请好友加入群聊
        */
        GROUP_MEMBER_INVITE_REQUEST = 41,
        GROUP_MEMBER_INVITE_SUCCESS = 42,
        GROUP_MEMBER_INVITE_FAILED = 43,
        // 服务端推送：当前在线用户被邀请加入群聊，客户端应刷新会话列表。
        GROUP_MEMBER_INVITED_PUSH = 44
    };
    /*
       单个Body允许的最大长度

       防止恶意客户端伪造一个超大的bodyLength
   */
    constexpr std::uint32_t MAX_BODY_LENGTH = 1024 * 1024;

    /*
        当前版本协议
    */
    constexpr std::uint16_t VERSION = 1;

#pragma pack(push, 1)
    /*
        数据包Header
    */
    struct Header
    {
        std::uint32_t magic;
        std::uint16_t version;
        std::uint16_t command;
        std::uint32_t body_length;
        std::uint64_t request_id;
    };
#pragma pack(pop)
    /*
        协议保护尺寸
    */
    static_assert(sizeof(Header) == 20, "Protocol Header size must be 20 bytes");

    /*
        Header长度
    */
    constexpr std::size_t HEADER_SIZE = sizeof(Header);

    /*
        数据包结构体
        有三个参数:
        std::uint16_t m_command
        std::string m_body
        std::uint64_t m_requestId
    */
    struct PacketFrame
    {
        PacketFrame() = default;
        PacketFrame(std::uint16_t command,
                    std::string body,
                    std::uint64_t requestId)
            : m_command(command),
              m_body(body),
              m_requestId(requestId) {};
        std::uint16_t m_command{0};
        std::string m_body;
        std::uint64_t m_requestId{0};
    };
}