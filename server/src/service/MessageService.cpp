#include "service/MessageService.h"

#include <cstdint>
#include <iostream>

/*
目前响应格式统一为
例:
{
  "code":
  "message":
}
*/
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

    bool isUnsignedInteger(const nlohmann::json &value)
    {
        if (value.is_number_unsigned())
        {
            return true;
        }
        return value.is_number_integer() && value.get<std::int64_t>() >= 0;
    }

    // 统一生成服务器响应Body;以后替换响应格式只需要集中修改这里
    protocolHeader::CommandType setResponse(
        std::string &responseBody,
        protocolHeader::CommandType command,
        const std::string &message)
    {
        const nlohmann::json response = {
            {protocolBody::CODE, static_cast<std::uint16_t>(command)},
            {protocolBody::MESSAGE, message}};

        responseBody = response.dump();
        return command;
    }
    protocolHeader::CommandType setResponse(
        std::string &responseBody,
        protocolHeader::CommandType command,
        const std::string &message,
        const std::vector<HistoricalMessage> &contents,
        const std::uint64_t nextCursor)
    {
        nlohmann::json content = nlohmann::json::array();

        for (const auto &p : contents)
        {
            content.push_back({{protocolBody::SEQ, p.seq},
                               {protocolBody::TEXT, p.text}});
        }

        const nlohmann::json response = {
            {protocolBody::CODE, static_cast<std::uint16_t>(command)},
            {protocolBody::MESSAGE, message},
            {protocolBody::CONTENT, std::move(content)},
            {protocolBody::NEXT_CURSOR, nextCursor}};

        responseBody = response.dump();
        return command;
    }
    MessageSendResult failed(std::string &responseBody, const std::string &message)
    {
        return {setResponse(responseBody, protocolHeader::CommandType::MESSAGE_SEND_FAILED, message),
                std::nullopt, false};
    }
}

MessageSendResult MessageService::sendContent(
    const std::string &body,
    std::string &responseBody,
    const std::uint64_t &userId,
    const std::string &username)
{

    nlohmann::json data;
    // 校验JSON、content、conversationId数据格式是否合格
    if (!validateSendContentRequest(body, data))
    {
        printf("数据类型校验不通过,消息发送失败\n");
        return failed(responseBody, "消息发送请求格式不正确");
    }

    // 检查content
    std::string content = data[protocolBody::CONTENT].get<std::string>();
    if (!isValidContent(content))
    {
        printf("消息内容校验不通过，消息发送失败\n");
        return failed(responseBody, "消息内容不能为空，且最多512个字符/2048字节");
    }

    // 检查conversation_id
    std::uint64_t conversationId = data[protocolBody::CONVERSATION_ID].get<std::uint64_t>();
    if (!isValidConversationId(conversationId))
    {
        printf("会话id校验不通过,消息发送失败\n");
        return failed(responseBody, "conversation_id 不合法");
    }

    // 检查client_msg_id
    const std::string &clientMsgId = data[protocolBody::CLIENT_MSG_ID].get_ref<std::string &>();
    if (!isValidClientMsgId(clientMsgId))
    {
        printf("客户端唯一消息id校验不通过,消息发送失败\n");
        return failed(responseBody, "client_msg_id 不合法");
    }

    // 当前只实现文本消息(type=1)，未实现的消息类型不能误入数据库。
    const auto typeValue = data[protocolBody::TYPE].get<std::uint64_t>();
    if (typeValue != 1)
    {
        return failed(responseBody, "当前仅支持文本消息(type=1)");
    }

    const auto type = static_cast<std::uint16_t>(typeValue);

    // 创建待保存的 Message；serverMsgId 和 seq 暂时未知，由 Repository 在数据库事务内补齐。
    Message pendingMessage(userId, username, content, conversationId, clientMsgId, type);
    const CreateMessageResult createResult = MessageRepository::createMessage(pendingMessage);

    if (createResult.status == CreateMessageStatus::ConversationNotFoundOrForbidden)
    {
        return failed(responseBody, "会话不存在，或当前用户无权在该会话发送消息");
    }
    if (createResult.status == CreateMessageStatus::ClientMsgIdConflict)
    {
        return failed(responseBody, "client_msg_id 已被用于另一条消息，请为新消息生成新的 ID");
    }
    if (createResult.status == CreateMessageStatus::Failed || !createResult.message.has_value())
    {
        return failed(responseBody, "服务器暂时无法保存消息，请稍后重试");
    }

    const Message &savedMessage = createResult.message.value();
    const auto responseCommand = setResponse(responseBody, protocolHeader::CommandType::MESSAGE_SEND_SUCCESS, "消息已保存");

    // 返回的是数据库已提交的结果。客户端可以用 last_seq 更新会话本地状态。
    nlohmann::json response = nlohmann::json::parse(responseBody);
    response[protocolBody::CONVERSATION_ID] = savedMessage.getConversationId();
    response[protocolBody::CLIENT_MSG_ID] = savedMessage.getClientMsgId();
    response[protocolBody::SERVER_MSG_ID] = savedMessage.getServerMsgId();
    response[protocolBody::SEQ] = savedMessage.getSeq();

    // 普通新消息的 last_seq 就是本条消息的 seq。
    // 若这是重试请求，在重试发送之前
    // 其他用户可能已经继续发了消息，因此尽量返回当前会话的真实 last_seq。
    std::uint64_t currentLastSeq = savedMessage.getSeq();
    if (createResult.status == CreateMessageStatus::Duplicate)
    {
        const auto lastSeqResult = ConversationRepository::getLastSeqByConversationId(
            savedMessage.getConversationId());
        if (lastSeqResult.status == ConversationQueryStatus::Found)
        {
            currentLastSeq = lastSeqResult.lastSeq;
        }
    }
    response[protocolBody::LAST_SEQ] = currentLastSeq;
    responseBody = response.dump();

    const bool shouldBroadcast = createResult.status == CreateMessageStatus::Created;
    return {responseCommand, savedMessage, shouldBroadcast};
}

protocolHeader::CommandType MessageService::MessageHistoryQuery(const std::string &body, std::string &responseBody, std::uint64_t userId)
{
    nlohmann::json data;
    // 校验JSON、content、conversationId数据格式是否合格
    if (!validateHistoricalMessageQueryRequest(body, data))
    {
        printf("数据类型校验不通过,查询历史消息失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::MESSAGE_HISTORY_FAILED, "查询历史信息请求格式不正确");
    }

    // 检查conversation_id
    std::uint64_t conversationId = data[protocolBody::CONVERSATION_ID].get<std::uint64_t>();
    if (!isValidConversationId(conversationId))
    {
        printf("会话id校验不通过,查询历史消息失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::MESSAGE_HISTORY_FAILED, "conversation_id 格式不正确");
    }

    // 检查 seq
    std::uint64_t seq = data[protocolBody::SEQ].get<std::uint64_t>();
    if (!isValidConversationId(seq))
    {
        printf("当前seq序号校验不通过,查询历史消息失败\n");
        return setResponse(responseBody, protocolHeader::CommandType::MESSAGE_HISTORY_FAILED, "seq 格式不正确");
    }

    // 查询历史消息 
    auto res = MessageRepository::queryHistoricalMessage(conversationId, seq,userId);
    if (res.status == QueryHistoricalMessageStatus::Error)
    {
        return setResponse(responseBody, protocolHeader::CommandType::MESSAGE_HISTORY_FAILED, "服务器暂时无法处理历史消息查询请求，请稍后再试");
    }
    else if(res.status == QueryHistoricalMessageStatus::Forbidden)
    {
        return setResponse(responseBody,protocolHeader::CommandType::MESSAGE_HISTORY_FAILED,"无权查询该会话的历史消息");
    }
    else if (res.status == QueryHistoricalMessageStatus::NotFound)
    {
        return setResponse(responseBody, protocolHeader::CommandType::MESSAGE_HISTORY_SUCCESS, "暂无历史消息",std::vector<HistoricalMessage>{},0);
    }

    const auto &contents = res.contents.value();
    // 第一条消息的seq 就是下一页查询使用的游标
    const std::uint64_t nextCursor = contents.front().seq;
    printf("查询历史消息成功\n");

    return setResponse(responseBody, protocolHeader::CommandType::MESSAGE_HISTORY_SUCCESS, "查询历史消息成功", contents,nextCursor);
}

bool MessageService::validateSendContentRequest(const std::string &body, nlohmann::json &data)
{
    try
    {
        data = nlohmann::json::parse(body);
    }
    catch (const nlohmann::json::parse_error &)
    {
        std::cerr << "消息 Body JSON 格式错误\n";
        return false;
    }

    if (!data.is_object())
    {
        std::cerr << "JSON Body必须是对象\n";
        return false;
    }

    if (!data.contains(protocolBody::CONTENT) || !data[protocolBody::CONTENT].is_string())
    {
        std::cerr << "content 不存在或者不是字符串\n";
        return false;
    }

    if (!data.contains(protocolBody::CONVERSATION_ID) ||
        !isUnsignedInteger(data[protocolBody::CONVERSATION_ID]))
    {
        std::cerr << "conversation_id 不存在或者不是非负整数\n";
        return false;
    }

    if (!data.contains(protocolBody::CLIENT_MSG_ID) || !data[protocolBody::CLIENT_MSG_ID].is_string())
    {
        std::cerr << "client_msg_id 不存在或者不是字符串\n";
        return false;
    }

    if (!data.contains(protocolBody::TYPE) || !isUnsignedInteger(data[protocolBody::TYPE]))
    {
        std::cerr << "last_seq 不存在或者不是非负整数\n";
        return false;
    }

    return true;
}

bool MessageService::validateHistoricalMessageQueryRequest(const std::string &body, nlohmann::json &data)
{
    try
    {
        data = nlohmann::json::parse(body);
    }
    catch (const nlohmann::json::parse_error &)
    {
        std::cerr << "消息 Body JSON 格式错误\n";
        return false;
    }

    if (!data.is_object())
    {
        std::cerr << "JSON Body必须是对象\n";
        return false;
    }

    if (!data.contains(protocolBody::CONVERSATION_ID) ||
        !isUnsignedInteger(data[protocolBody::CONVERSATION_ID]))
    {
        std::cerr << "conversation_id 不存在或者不是非负整数\n";
        return false;
    }

    if (!data.contains(protocolBody::SEQ) ||
        !isUnsignedInteger(data[protocolBody::SEQ]))
    {
        std::cerr << "conversation_id 不存在或者不是非负整数\n";
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
    return conversationId > 0;
}

bool MessageService::isValidClientMsgId(const std::string &clientMsgId)
{
    return !clientMsgId.empty() && clientMsgId.size() <= 64;
}

bool MessageService::isValidSeq(const std::uint64_t &seq)
{
    return seq >= 0;
}
