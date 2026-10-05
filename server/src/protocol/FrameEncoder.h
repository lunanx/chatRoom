#include "protocol/ProtocolHeader.h"




/*
    打包服务器数据包
    此类设置为无成员变量，避免新packet覆盖旧packet
*/
class FrameEncoder
{
public:
    std::string buildBufPacket(
        std::uint16_t m_command,
        std::uint64_t m_requestId,
        const nlohmann::json &body);
};
