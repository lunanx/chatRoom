#pragma once
/*
    客户端与服务端公共 JSON 字段格式
    也就是body部分格式

    先一注册功能为主，其他需要再添加
*/
namespace protocolHeader
{
    /*
        用户相关
    */
    constexpr const char* USER_ID = "user_id"; // 数据库中的用户唯一标识id

    constexpr const char* USERNAME = "username"; // 用户名

    constexpr const char* PASSWORD = "password"; // 密码

    constexpr const char* NICKNAME = "nickname"; // 昵称

    constexpr const char* AVATAR = "avatar"; // 头像资源地址

    /*
        通用结果--一般是服务器给客户端
    */
    constexpr const char* CODE = "code"; // 状态码

    constexpr const char* MESSAGE = "message"; // 结果具体信息

    constexpr const char* DATA = "data"; // 发送的数据

}
