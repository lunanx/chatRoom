#include "ConversationService.h"
#include "protocol/ProtocolBody.h"
#include "repository/UserRepository.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include <utility>

namespace
{

    protocolHeader::CommandType commandForType(std::uint64_t type, bool success)
    {
        if (type == 1)
        {
            return success ? protocolHeader::CommandType::PRIVATECREATE_SUCCESS
                           : protocolHeader::CommandType::PRIVATECREATE_FAILED;
        }
        return success ? protocolHeader::CommandType::GROUPCREATE_SUCCESS
                       : protocolHeader::CommandType::GROUPCREATE_FAILED;
    }

    // 统一生成服务器响应Body;以后替换响应格式只需要集中修改这里
    protocolHeader::CommandType setResponse(
        std::string &responseBody,
        protocolHeader::CommandType command,
        const std::string &message,
        nlohmann::json data = nullptr)
    {
        nlohmann::json response = {
            {protocolBody::CODE, static_cast<std::uint16_t>(command)},
            {protocolBody::MESSAGE, message}};

        if (!data.is_null())
        {
            response[protocolBody::DATA] = std::move(data);
        }
        responseBody = response.dump();
        return command;
    }

    bool isUnsignedInteger(const nlohmann::json &value)
    {
        if (value.is_number_unsigned())
        {
            return true;
        }
        return value.is_number_integer() && value.get<std::int64_t>() >= 0;
    }

    ConversationCreateResult fail(
        std::string &responseBody,
        std::uint64_t type,
        const std::string &message)
    {
        return {setResponse(responseBody, commandForType(type, false), message), std::nullopt, std::nullopt};
    }
}

ConversationCreateResult ConversationService::createConversation(
    const std::string &body,
    std::string &responseBody,
    const std::uint64_t &authenticatedUserId)
{

    nlohmann::json data;
    ConversationCreateRequestResult res = validateConversationCreateRequest(body, data);
    if (res == ConversationCreateRequestResult::RequestError)
    {
        printf("数据类型校验不通过,创建会话失败\n");
        return {setResponse(responseBody, protocolHeader::CommandType::PRIVATECREATE_FAILED, "会话创建请求格式不正确")};
    }

    std::uint64_t type = data[protocolBody::TYPE].get<std::uint64_t>();
    Conversation conversation = [&]() -> Conversation
    {
        if (type == 1)
        {
            // 客户端新建/打开私聊只需要传对方 ID；自己的身份必须取自服务端连接。
            std::uint64_t userId1 = authenticatedUserId;
            std::uint64_t userId2 = data.contains(protocolBody::TARGET_USER_ID)
                                        ? data[protocolBody::TARGET_USER_ID].get<std::uint64_t>()
                                        : data[protocolBody::USER_ID_2].get<std::uint64_t>();
            if (!data.contains(protocolBody::TARGET_USER_ID))
            {
                userId1 = data[protocolBody::USER_ID_1].get<std::uint64_t>();
                userId2 = data[protocolBody::USER_ID_2].get<std::uint64_t>();
            }
            // 私聊双方排序，确保同一对用户不会产生 A-B 与 B-A 两条会话。
            if (userId1 > userId2)
            {
                std::swap(userId1, userId2);
            }
            return Conversation(static_cast<std::uint16_t>(type), userId1, userId2);
        }
        return Conversation(static_cast<std::uint16_t>(type), data[protocolBody::GROUP_ID].get<std::uint64_t>());
    }();
    /*
        私聊验证:
        服务端验证当前登录用户确实是两个参与者之一，并检查两名用户的账号状态。
        然后将用户 ID 排序，保证相同的两个人不会产生 A-B 和 B-A 两条不同的私聊记录
    */
    if (type == 1)
    {
        const auto userId1 = conversation.getUserId1().value();
        const auto userId2 = conversation.getUserId2().value();
        if (userId1 == 0 || userId2 == 0 || userId1 == userId2)
        {
            return fail(responseBody, type, "私聊用户 ID 不合法");
        }
        if (authenticatedUserId != userId1 && authenticatedUserId != userId2)
        {
            return fail(responseBody, type, "当前登录用户必须是私聊参与者");
        }
        const auto firstUserStatus = UserRepository::checkActiveUserId(userId1);
        const auto secondUserStatus = UserRepository::checkActiveUserId(userId2);
        if (firstUserStatus == UserQueryStatus::Error || secondUserStatus == UserQueryStatus::Error)
        {
            return fail(responseBody, type, "服务器暂时无法验证私聊用户，请稍后重试");
        }
        if (firstUserStatus != UserQueryStatus::Found || secondUserStatus != UserQueryStatus::Found)
        {
            return fail(responseBody, type, "私聊参与者不存在或账号不可用");
        }
    }
    /*
        群聊验证:
        检查群组状态和当前用户的成员身份，避免仅凭客户端传来的 group_id 就创建或获取会话。
    */
    else
    {
        const std::uint64_t groupId = conversation.getGroupId().value();
        if (groupId == 0)
        {
            return fail(responseBody, type, "群组 ID 不合法");
        }
        const auto membershipStatus = ConversationRepository::checkActiveGroupMember(groupId, authenticatedUserId);
        if (membershipStatus == ConversationQueryStatus::Error)
        {
            return fail(responseBody, type, "服务器暂时无法验证群组成员，请稍后重试");
        }
        if (membershipStatus != ConversationQueryStatus::Found)
        {
            return fail(responseBody, type, "群组不存在、已禁用或当前用户不是群成员");
        }
    }

    // 先查询已有会话
    // 如果不存在才能新创建
    // 如果重复打开私聊/群聊时,返回原有 conversation_id。
    auto idResult = ConversationRepository::getConversationIdByConversationObject(conversation);
    if (idResult.status == ConversationQueryStatus::Error)
    {
        return fail(responseBody, type, "服务器暂时无法查询会话，请稍后重试");
    }
    // NotFound，说明没有conversation_id
    if (idResult.status == ConversationQueryStatus::NotFound)
    {
        const auto createResult = type == 1
                                      ? ConversationRepository::createPrivateConversation(conversation)
                                      : ConversationRepository::createGroupConversation(conversation);

        if (createResult == CreateConversationResult::Created)
        {
            // 创建成功了，要获取conversation_id。
            idResult = ConversationRepository::getConversationIdByConversationObject(conversation);
        }
        else
        {
            // 另一请求可能在我们的查询之后先创建了同一会话；
            // 再次查找可兼容此并发情况。
            idResult = ConversationRepository::getConversationIdByConversationObject(conversation);
        }
    }

    if (idResult.status != ConversationQueryStatus::Found)
    {
        return fail(responseBody, type, "服务器暂时无法创建或获取会话，请稍后重试");
    }

    // 以上步骤是如果已经存在会话就返回原会话id,不存在就创建新会话id

    // 以下步骤就是返回旧或新的会话信息
    // 获取当前数据库真实的last_seq
    const auto lastSeqResult = ConversationRepository::getLastSeqByConversationId(idResult.conversationId);
    if (lastSeqResult.status != ConversationQueryStatus::Found)
    {
        return fail(responseBody, type, "会话已获取，但读取消息序号失败，请稍后重试");
    }
    // 获取当前的会话id
    conversation.setId(idResult.conversationId);
    const auto responseCommand = commandForType(type, true);
    nlohmann::json response = {
        {protocolBody::CODE, static_cast<std::uint16_t>(responseCommand)},
        {protocolBody::MESSAGE, "获取会话成功"},
        {protocolBody::CONVERSATION_ID, idResult.conversationId},
        {protocolBody::LAST_SEQ, lastSeqResult.lastSeq}};
    responseBody = response.dump();

    return {responseCommand, idResult.conversationId, lastSeqResult.lastSeq};
}

protocolHeader::CommandType ConversationService::listConversations(
    std::string &responseBody,
    std::uint64_t authenticatedUserId)
{
    using protocolHeader::CommandType;
    if (authenticatedUserId == 0)
    {
        return setResponse(responseBody, CommandType::QUERY_CONVERSATION_LIST_FAILED,
                           "当前登录用户无效，请重新登录");
    }

    std::vector<ConversationListEntry> conversations;
    const auto status = ConversationRepository::getConversationsForUser(
        authenticatedUserId, conversations);
    if (status == ConversationQueryStatus::Error)
    {
        return setResponse(responseBody, CommandType::QUERY_CONVERSATION_LIST_FAILED,
                           "服务器暂时无法查询会话列表，请稍后重试");
    }

    nlohmann::json items = nlohmann::json::array();
    for (const auto &item : conversations)
    {
        items.push_back({{protocolBody::CONVERSATION_ID, item.conversationId},
                         {protocolBody::TYPE, item.type},
                         {protocolBody::LAST_SEQ, item.lastSeq},
                         {protocolBody::TARGET_USER_ID, item.targetUserId},
                         {protocolBody::USERNAME, item.targetUsername},
                         {protocolBody::DISPLAY_NAME, item.displayName},
                         {protocolBody::AVATAR, item.avatar},
                         {protocolBody::LAST_MESSAGE, item.lastMessage},
                         {protocolBody::LAST_TIME, item.lastTime}});
    }
    return setResponse(responseBody, CommandType::QUERY_CONVERSATION_LIST_SUCCESS,
                       "查询会话列表成功", std::move(items));
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

    if (!data.contains(protocolBody::TYPE) ||
        !isUnsignedInteger(data[protocolBody::TYPE]))
    {
        printf("type类型不正确\n");
        return ConversationCreateRequestResult::RequestError;
    }

    const auto type = data[protocolBody::TYPE].get<std::uint64_t>();
    if (type == 1)
    {
        const bool targetFieldProvided = data.contains(protocolBody::TARGET_USER_ID);
        const bool hasTargetUserId = targetFieldProvided &&
                                     isUnsignedInteger(data[protocolBody::TARGET_USER_ID]);
        const bool hasLegacyUserPair = !targetFieldProvided &&
                                       data.contains(protocolBody::USER_ID_1) && isUnsignedInteger(data[protocolBody::USER_ID_1]) &&
                                       data.contains(protocolBody::USER_ID_2) && isUnsignedInteger(data[protocolBody::USER_ID_2]);
        if (!hasTargetUserId && !hasLegacyUserPair)
        {
            printf("私聊请求缺少有效的 target_user_id 或用户 ID 对\n");
            return ConversationCreateRequestResult::RequestError;
        }
        return ConversationCreateRequestResult::IsPrivateConversation;
    }

    if (type == 2)
    {
        if (!data.contains(protocolBody::GROUP_ID) || !isUnsignedInteger(data[protocolBody::GROUP_ID]))
        {
            printf("是群聊，但不存在群组id或类型不正确\n");
            return ConversationCreateRequestResult::RequestError;
        }
        return ConversationCreateRequestResult::IsGroupConversation;
    }

    return ConversationCreateRequestResult::RequestError;
}
