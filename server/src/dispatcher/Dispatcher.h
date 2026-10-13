#pragma once

#include "protocol/ProtocolHeader.h"
#include "service/UserService.h"
#include "net/ClientSession.h"
class Dispatcher
{
public:
    Dispatcher() = default;
    /*
        命令调度器
    */
    protocolHeader::PacketFrame dispatch(const protocolHeader::PacketFrame &frame);

    /*
        设置m_cli
    */
    void setCliSession(const ClientSession& cli);
private:
    ClientSession m_cli;
};
