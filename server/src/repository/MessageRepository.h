#pragma once 

#include "db/DatabaseManager.h"
#include "model/Message.h"
#include <string>

enum class CreateMessageResult
{
    Created,
    Failed
};

class MessageRepository
{
public:
    /*
        创建消息
    */
    static CreateMessageResult createMessage(const Message& message);
};

