#include <cstdint>
#include <cstddef>

/*
   =============================
   客户端 / 服务器公共通信协议
   =============================

   数据包结构:
   ----------Header------------------
    magic         4 bytes
    version       2 bytes
    command       2 bytes
    request_id    8 bytes
    body_length   4 bytes
    ----------Body-------------------
        JSON UTF-8
    ---------------------------------

    Header总长度: 20 bytes

    整数用网络字节序---通过主机转网络

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
        PUBLIC_MESSAGE_SEND = 10,
        PUBLIC_MESSAGE_RECV = 11,

        /*
            私聊消息
        */
        PRIVATE_MESSAGE_SEND = 16,
        PRIVATE_MESSAGE_RECV = 17,

        /*
            用户上线 / 下线
        */
        USER_JOIN = 18,
        USER_LEAVE = 19,
    };

    /*
       单个Body允许的最大长度

       防止恶意客户端伪造一个超大的bodyLength
   */
    constexpr std::uint32_t MAX_BODY_LENGTH = 1024 * 1024;


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

    /*
        Header长度
    */
    constexpr std::size_t HEADER_SIZE = sizeof(Header);
}