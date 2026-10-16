#include "ConversationService.h"

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

ConversationCreateResult ConversationService::createConversation(const std::string &body, std::string &responseBody)
{
    /*
       TODO:
       1 校验JSON合不合法
       2 校验type,user_id_1,user_id_1
       3 创建Conversation对象
       4 ConversationRepository保存
       5 返回会话创建结构体
   */
    nlohmann::json data;
    ConversationCreateRequestResult res = validateConversationCreateRequest(body, data);
    if (res == ConversationCreateRequestResult::RequestError)
    {
        printf("数据类型校验不通过,创建会话失败\n");
        return {setResponse(responseBody, protocolHeader::CommandType::PRIVATECREATE_FAILED, "会话创建请求格式不正确")};
    }

    if (res == ConversationCreateRequestResult::IsPrivateConversation)
    {
        // 检查type是否为 1
        std::uint64_t type = data[protocolHeader::TYPE].get<std::uint64_t>();
        if (!isValidType(type, 1))
        {
            printf("会话类型校验不通过，会话创建失败\n");
            return {setResponse(responseBody, protocolHeader::CommandType::PRIVATECREATE_FAILED, "会话格式不正确")};
        }
        // 检查两个用户id
        std::uint64_t userId1 = data[protocolHeader::USER_ID_1].get<std::uint64_t>();
        std::uint64_t userId2 = data[protocolHeader::USER_ID_2].get<std::uint64_t>();
        if (!(isValidUserId(userId1) && isValidUserId(userId2)))
        {
            printf("用户id校验不通过,会话创建失败\n");
            return {setResponse(responseBody, protocolHeader::CommandType::PRIVATECREATE_FAILED, "用户或群组id格式不正确")};
        }

        // 构造Conversation
        Conversation conversation(type, userId1, userId2);
        // 保存至数据库
        if (ConversationRepository::createPrivateConversation(conversation) == CreateConversationResult::Failed)
        {
            printf("数据库创建会话失败\n");
            return {setResponse(responseBody, protocolHeader::CommandType::PRIVATECREATE_FAILED, "服务器暂时无法处理会话创建请求，请稍后再试")};
        }

        getConversationIdResult result = ConversationRepository::getConversationIdByConversationObject(conversation);
        if (result.status == ConversationQueryStatus::Error)
        {
            printf("创建会话成功,但数据库层有错误\n");
            return {setResponse(responseBody, protocolHeader::CommandType::PRIVATECREATE_FAILED, "服务器暂时无法处理会话创建请求，请稍后再试")};
        }
        else if (result.status == ConversationQueryStatus::NotFound)
        {
            printf("创建会话成功，但查询conversation_id失败\n");
            return {setResponse(responseBody, protocolHeader::CommandType::PRIVATECREATE_FAILED, "服务器暂时无法处理会话创建请求，请稍后再试")};
        }
        conversation.setId(result.conversationId);

        printf("创建会话与查询conversation_id都成功\n");
        return {setResponse(responseBody, protocolHeader::CommandType::PRIVATECREATE_SUCCESS, "创建会话成功"), conversation.getId()};
    }
    else
    {
        // 检查type是否为 2
        std::uint64_t type = data[protocolHeader::TYPE].get<std::uint64_t>();
        if (!isValidType(type, 2))
        {
            printf("会话类型校验不通过，会话创建失败\n");
            return {setResponse(responseBody, protocolHeader::CommandType::GROUPCREATE_FAILED, "会话格式不正确")};
        }
        // 检查群组id
        std::uint64_t groupId = data[protocolHeader::GROUP_ID].get<std::uint64_t>();
        if (!isValidGroupId(groupId))
        {
            printf("群组id校验不通过,会话创建失败\n");
            return {setResponse(responseBody, protocolHeader::CommandType::GROUPCREATE_FAILED, "用户或群组id格式不正确")};
        }

        // 构造Conversation
        Conversation conversation(type,groupId);
        // 保存至数据库
        if (ConversationRepository::createGroupConversation(conversation) == CreateConversationResult::Failed)
        {
            printf("数据库创建会话失败\n");
            return {setResponse(responseBody, protocolHeader::CommandType::GROUPCREATE_FAILED, "服务器暂时无法处理会话创建请求，请稍后再试")};
        }

        getConversationIdResult result = ConversationRepository::getConversationIdByConversationObject(conversation);
        if (result.status == ConversationQueryStatus::Error)
        {
            printf("创建会话成功,但数据库层有错误\n");
            return {setResponse(responseBody, protocolHeader::CommandType::GROUPCREATE_FAILED, "服务器暂时无法处理会话创建请求，请稍后再试")};
        }
        else if (result.status == ConversationQueryStatus::NotFound)
        {
            printf("创建会话成功，但查询conversation_id失败\n");
            return {setResponse(responseBody, protocolHeader::CommandType::GROUPCREATE_FAILED, "服务器暂时无法处理会话创建请求，请稍后再试")};
        }
        conversation.setId(result.conversationId);

        printf("创建会话与查询conversation_id都成功\n");
        return {setResponse(responseBody, protocolHeader::CommandType::GROUPCREATE_SUCCESS, "创建会话成功"), conversation.getId()};
    }
}

ConversationCreateRequestResult ConversationService::validateConversationCreateRequest(const std::string &body, nlohmann::json &data)
{
    try
    {
        data = nlohmann::json::parse(body);
    }
    catch (const nlohmann::json::parse_error &)
    {
        printf("JSON 格式错误\n");
        return ConversationCreateRequestResult::RequestError;
    }

    if (!data.is_object())
    {
        printf("JSON Body必须是对象\n");
        return ConversationCreateRequestResult::RequestError;
    }

    if (!data.contains(protocolHeader::TYPE) || !data[protocolHeader::TYPE].is_number_unsigned())
    {
        printf("content 不存在或者不是非负整数\n");
        return ConversationCreateRequestResult::RequestError;
    }

    if // 用户1和用户2要同时存在
        ((data.contains(protocolHeader::USER_ID_1) && data[protocolHeader::USER_ID_1].is_number_unsigned()) &&
         (data.contains(protocolHeader::USER_ID_2) && data[protocolHeader::USER_ID_2].is_number_unsigned()))
    {
        printf("该会话是私聊,Reques格式正确\n");
        return ConversationCreateRequestResult::IsPrivateConversation;
    }
    else // 不是私聊，看看是不是群聊
    {
        if (data.contains(protocolHeader::GROUP_ID) && data[protocolHeader::GROUP_ID].is_number_unsigned())
        {
            printf("该会话是群聊,Reques格式正确\n");
            return ConversationCreateRequestResult::IsGroupConversation;
        }
        printf("不存在userId1和userId2 或者 groupId或者字段不是非负整数\n");
        return ConversationCreateRequestResult::RequestError;
    }
}

bool ConversationService::isValidType(const std::uint64_t &type, std::uint64_t check)
{
    return type == check;
}

bool ConversationService::isValidUserId(const std::uint64_t &userId)
{
    return true;
}

bool ConversationService::isValidGroupId(const std::uint64_t &groupId)
{
    return true;
}
