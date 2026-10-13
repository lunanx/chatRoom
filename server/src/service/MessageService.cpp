#include "service/MessageService.h"

namespace
{
    // JSON 解析器会检查 UTF-8；因此统计非 UTF-8 延续字节的数量即可得到 Unicode 码点数。
    // 这比直接使用 std::string::size() 更接近普通用户理解的“字符数”。
    std::size_t utf8CharacterCount(const std::string &value)
    {
        std::size_t count = 0;
        for (unsigned char ch : value)
        {
            // 多字节的非首个字节的前两位都是 10
            if ((ch & 0xC0) != 0x80)
            {
                ++count;
            }
        }
        return count;
    }

    // 统一生成服务器响应Body;以后替换响应格式只需要集中修改这里
    protocolHeader::CommandType setResponse(
        std::string &responseBody,
        protocolHeader::CommandType command,
        const std::string &message)
    {
        const nlohmann::json response = {
            {protocolHeader::CODE, static_cast<std::uint16_t>(command)},
            {protocolHeader::MESSAGE, message}};

        responseBody = response.dump();
        return command;
    }
}

protocolHeader::CommandType MessageService::sendContent(const std::string &body, std::string &responseBody,const std::uint64_t& userId,const std::string& username)
{
    /*
        TODO:
        1 校验JSON合不合法
        2 校验content
        3 创建Message对象
        4 MessageRepository保存
        5 返回消息发送结果
    */

    nlohmann::json data;
    // 校验JSON、content数据格式是否合格
    if (!validateSendContentRequest(body, data))
    {
        printf("数据类型校验不通过,消息发送失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::MESSAGE_SEND_FAILED, "消息发送请求格式不正确");
    }

    // 检查content
    std::string content = data[protocolHeader::CONTENT].get<std::string>();
    if (!isValidContent(content))
    {
        printf("消息内容校验不通过，消息发送失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::MESSAGE_SEND_FAILED, "消息格式不正确");
    }
    /* 
        TODO:
        1 将senderId,senderName,content创建成Message对象
        2 保存至数据库，--其实也就是为了消息持久化，这个可以现不着急
        3 返回发送情况，首先既要CommandType,又要返回广播时所需要的消息，那就返回值应该要换成结构体了
    */
}

bool MessageService::validateSendContentRequest(const std::string &body, nlohmann::json &data)
{
    try
    {
        data = nlohmann::json::parse(body);
    }
    catch (const nlohmann::json::parse_error &)
    {
        printf("JSON 格式错误\n");
        return false;
    }

    if (!data.is_object())
    {
        printf("JSON Body必须是对象\n");
        return false;
    }

    if (!data.contains(protocolHeader::CONTENT) || !data[protocolHeader::CONTENT].is_string())
    {
        printf("content 不存在或者不是字符串\n");
        return false;
    }

    return true;
}

bool MessageService::isValidContent(const std::string &content)
{
    return content.empty() || (utf8CharacterCount(content) <= 512 && content.size() <= 2048);
}
