#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

class Message
{
private:
public:
    Message() = default;
    Message(
        const std::uint64_t &senderId,
        const std::string &senderName,
        const std::string &content);

    const std::uint64_t& getSenderId() const;
    const std::string& getSenderName() const;
    const std::string& getContent() const;
private:
    std::uint64_t m_senderId;  // 表示谁发的,这个可由登录验证后直接得到
    std::string m_senderName; // 广播显示的名字,这个可由登录验证后直接得到
    std::string m_content;     // 消息内容
};
