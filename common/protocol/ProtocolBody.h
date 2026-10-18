#pragma once
/*
    客户端与服务端公共 JSON 字段格式
    也就是body部分格式
*/
namespace protocolBody
{
    /*
        通用id字段
    */
    constexpr const char *ID = "id";

    /*
        用户相关
    */

    constexpr const char *USER_ID = "user_id"; // 数据库中的用户唯一标识id

    constexpr const char *USERNAME = "username"; // 用户名

    constexpr const char *PASSWORD = "password"; // 密码

    constexpr const char *PASSWORD_HASH = "password_hash"; // 密码

    constexpr const char *NICKNAME = "nickname"; // 昵称

    constexpr const char *AVATAR = "avatar"; // 头像资源地址

    /*
        通用结果--一般是服务器给客户端
    */
    constexpr const char *CODE = "code"; // 状态码

    constexpr const char *MESSAGE = "message"; // 结果具体信息

    constexpr const char *DATA = "data"; // 发送的数据

    /*
        聊天消息所需类型
    */
    constexpr const char *CONTENT = "content";
    constexpr const char *CONVERSATION_ID = "conversation_id";
    constexpr const char *CLIENT_MSG_ID = "client_msg_id";
    constexpr const char *SERVER_MSG_ID = "server_msg_id";
    constexpr const char *SEQ = "seq";
    /*
        创建会话相关
    */
    constexpr const char *TYPE = "type";
    constexpr const char *USER_ID_1 = "user_id_1";
    constexpr const char *USER_ID_2 = "user_id_2";
    constexpr const char *GROUP_ID = "group_id";
    constexpr const char *LAST_SEQ = "last_seq";

    /*
        历史消息相关
    */
    constexpr const char *HITSTORICALMESSAGESPLIT = "###";

    constexpr const char *TEXT = "text";

    constexpr const char *NEXT_CURSOR = "next_cursor";
}
