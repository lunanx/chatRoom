#pragma once 

#include "protocol/ProtocolHeader.h"

class Dispatcher
{
public:
    protocolHeader::PacketFrame dispatch(protocolHeader::PacketFrame frame);
};

