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

MessageSendResult MessageService::sendContent(const std::string &body, std::string &responseBody,const std::uint64_t& userId,const std::string& username)
{
    /*
        TODO:
        1 校验JSON合不合法
        2 校验content
        3 创建Message对象
        4 MessageRepository保存
        5 返回消息发送结果

        这里的CommandType设置的语义可能有点问题-但先不着急改，之后在处理
    */

    nlohmann::json data;
    // 校验JSON、content、conversationId数据格式是否合格
    if (!validateSendContentRequest(body, data))
    {
        printf("数据类型校验不通过,消息发送失败\n");
        return {setResponse(responseBody, protocolHeader::CommandType::MESSAGE_SEND_FAILED, "消息发送请求格式不正确")};
    }

    // 检查content
    std::string content = data[protocolHeader::CONTENT].get<std::string>();
    if (!isValidContent(content))
    {
        printf("消息内容校验不通过，消息发送失败\n");
        return {setResponse(responseBody, protocolHeader::CommandType::MESSAGE_SEND_FAILED, "消息格式不正确")};
    }
    // 检查conversation_id--但感觉没啥好检查的--想想需要什么约束吗
    std::uint64_t conversationId = data[protocolHeader::CONVERSATION_ID].get<std::uint64_t>();
    if(!isValidConversationId(conversationId))
    {
        printf("会话id校验不通过,消息发送失败\n");
        return {setResponse(responseBody, protocolHeader::CommandType::MESSAGE_SEND_FAILED, "会话id格式不正确")};
    }

    // 构造Message
    Message message(userId,username,content,conversationId);
    //保存至数据库-----这里由于数据字段不够，还不实现
    // const CreateMessageResult createResult = MessageRepository::createMessage(message);
    // if(createResult == CreateMessageResult::Failed)
    // {
    //     printf("数据库创建消息失败\n");
    //     return {setResponse(responseBody, protocolHeader::CommandType::MESSAGE_SEND_FAILED, "服务器暂时无法处理消息创建请求，请稍后再试")};
    // }

    printf("消息创建成功\n");
    return {setResponse(responseBody, protocolHeader::CommandType::MESSAGE_SEND_SUCCESS, "消息创建成功"),message};
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

    if(!data.contains(protocolHeader::CONVERSATION_ID) || !data[protocolHeader::CONVERSATION_ID].is_number_unsigned())
    {
        printf("conversation_id 不存在或者不是非负整数");
        return false;
    }

    return true;
}

bool MessageService::isValidContent(const std::string &content)
{
    return !content.empty() && (utf8CharacterCount(content) <= 512 && content.size() <= 2048);
}

bool MessageService::isValidConversationId(const std::uint64_t &conversationId)
{
    return true;
}
