#include "protocol/FrameDecoder.h"
#include "protocol/FrameEncoder.h"
#include "protocol/ProtocolHeader.h"

#include <arpa/inet.h>
#include <endian.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct TestFailure : std::runtime_error
    {
        explicit TestFailure(const std::string &message)
            : std::runtime_error(message)
        {
        }
    };

#define CHECK(condition)                                                                    \
    do                                                                                      \
    {                                                                                       \
        if (!(condition))                                                                   \
        {                                                                                   \
            std::ostringstream oss;                                                         \
            oss << "CHECK failed: " << #condition << " (" << __FILE__ << ":" << __LINE__ \
                << ")";                                                                    \
            throw TestFailure(oss.str());                                                   \
        }                                                                                   \
    } while (false)

    std::string buildRawFrame(std::uint16_t command,
                              std::uint64_t requestId,
                              const std::string &body)
    {
        protocolHeader::Header header{};
        header.magic = htonl(protocolHeader::MAGIC);
        header.version = htons(protocolHeader::VERSION);
        header.command = htons(command);
        header.body_length = htonl(static_cast<std::uint32_t>(body.size()));
        header.request_id = htobe64(requestId);

        std::string packet;
        packet.reserve(protocolHeader::HEADER_SIZE + body.size());
        packet.append(reinterpret_cast<const char *>(&header), protocolHeader::HEADER_SIZE);
        packet.append(body);
        return packet;
    }

    void checkFrame(const DecodedFrame &frame,
                    std::uint16_t command,
                    std::uint64_t requestId,
                    const std::string &body)
    {
        CHECK(frame.m_command == command);
        CHECK(frame.m_requestId == requestId);
        CHECK(frame.m_body == body);
    }

    void testEncoderDecoderRoundTrip()
    {
        FrameEncoder encoder;
        FrameDecoder decoder;

        const std::uint16_t command = static_cast<std::uint16_t>(protocolHeader::CommandType::LOGIN_REQUEST);
        const std::uint64_t requestId = 0x0102030405060708ULL;
        const nlohmann::json body = {
            {"username", "测试用户"},
            {"message", "你好，聊天室"}};

        const std::string expectedBody = body.dump();
        const std::string packet = encoder.buildBufPacket(command, requestId, body);

        CHECK(packet.size() == protocolHeader::HEADER_SIZE + expectedBody.size());

        size_t offset = 0;
        DecodedFrame output{};
        const DecoderStatus status = decoder.parseBufPacket(packet, output, offset);

        CHECK(status == DecoderStatus::PacketReady);
        checkFrame(output, command, requestId, expectedBody);
        CHECK(offset == packet.size());
    }

    void testNeedMoreDataForPartialHeader()
    {
        FrameEncoder encoder;
        FrameDecoder decoder;

        const std::string packet = encoder.buildBufPacket(
            static_cast<std::uint16_t>(protocolHeader::CommandType::LOGIN_REQUEST),
            100,
            nlohmann::json{{"msg", "hello"}});

        const std::string partial = packet.substr(0, 10);
        size_t offset = 0;
        DecodedFrame output{};

        const DecoderStatus status = decoder.parseBufPacket(partial, output, offset);

        CHECK(status == DecoderStatus::NeedMoreData);
        CHECK(offset == 0);
    }

    void testNeedMoreDataForPartialBody()
    {
        FrameEncoder encoder;
        FrameDecoder decoder;

        const std::string packet = encoder.buildBufPacket(
            static_cast<std::uint16_t>(protocolHeader::CommandType::LOGIN_REQUEST),
            101,
            nlohmann::json{{"msg", "hello"}});

        const std::string partial = packet.substr(0, packet.size() - 1);
        size_t offset = 0;
        DecodedFrame output{};

        const DecoderStatus status = decoder.parseBufPacket(partial, output, offset);

        CHECK(status == DecoderStatus::NeedMoreData);
        CHECK(offset == 0);
    }

    void testTwoFramesInOneBuffer()
    {
        FrameEncoder encoder;
        FrameDecoder decoder;

        const std::string bodyA = nlohmann::json{{"name", "A"}}.dump();
        const std::string bodyB = nlohmann::json{{"name", "B"}}.dump();

        const std::string packetA = encoder.buildBufPacket(
            static_cast<std::uint16_t>(protocolHeader::CommandType::LOGIN_REQUEST),
            200,
            nlohmann::json{{"name", "A"}});
        const std::string packetB = encoder.buildBufPacket(
            static_cast<std::uint16_t>(protocolHeader::CommandType::REGISTER_REQUEST),
            201,
            nlohmann::json{{"name", "B"}});

        const std::string buffer = packetA + packetB;
        size_t offset = 0;
        DecodedFrame output{};

        DecoderStatus status = decoder.parseBufPacket(buffer, output, offset);
        CHECK(status == DecoderStatus::PacketReady);
        checkFrame(output,
                   static_cast<std::uint16_t>(protocolHeader::CommandType::LOGIN_REQUEST),
                   200,
                   bodyA);
        CHECK(offset == packetA.size());

        status = decoder.parseBufPacket(buffer, output, offset);
        CHECK(status == DecoderStatus::PacketReady);
        checkFrame(output,
                   static_cast<std::uint16_t>(protocolHeader::CommandType::REGISTER_REQUEST),
                   201,
                   bodyB);
        CHECK(offset == buffer.size());

        status = decoder.parseBufPacket(buffer, output, offset);
        CHECK(status == DecoderStatus::NeedMoreData);
        CHECK(offset == buffer.size());
    }

    void testThreeFramesInOneBuffer()
    {
        FrameEncoder encoder;
        FrameDecoder decoder;

        const std::string packetA = encoder.buildBufPacket(
            static_cast<std::uint16_t>(protocolHeader::CommandType::LOGIN_REQUEST),
            300,
            nlohmann::json{{"id", 1}});
        const std::string packetB = encoder.buildBufPacket(
            static_cast<std::uint16_t>(protocolHeader::CommandType::REGISTER_REQUEST),
            301,
            nlohmann::json{{"id", 2}});
        const std::string packetC = encoder.buildBufPacket(
            static_cast<std::uint16_t>(protocolHeader::CommandType::LOGOUT_REQUEST),
            302,
            nlohmann::json{{"id", 3}});

        const std::string buffer = packetA + packetB + packetC;
        size_t offset = 0;
        int readyCount = 0;

        while (true)
        {
            DecodedFrame output{};
            const DecoderStatus status = decoder.parseBufPacket(buffer, output, offset);

            if (status == DecoderStatus::PacketReady)
            {
                ++readyCount;
                continue;
            }

            CHECK(status == DecoderStatus::NeedMoreData);
            break;
        }

        CHECK(readyCount == 3);
        CHECK(offset == buffer.size());
    }

    void testFragmentedFrame()
    {
        FrameEncoder encoder;
        FrameDecoder decoder;

        const std::string packet = encoder.buildBufPacket(
            static_cast<std::uint16_t>(protocolHeader::CommandType::LOGIN_REQUEST),
            400,
            nlohmann::json{{"message", "fragmented"}});

        std::string buffer;
        size_t offset = 0;
        DecodedFrame output{};

        const std::vector<std::pair<size_t, size_t>> chunks = {
            {0, 5},
            {5, 7},
            {12, packet.size() - 12}};

        CHECK(chunks[0].second + chunks[1].second + chunks[2].second == packet.size());

        DecoderStatus status = DecoderStatus::NeedMoreData;
        for (const auto &[start, length] : chunks)
        {
            buffer.append(packet.data() + start, length);
            status = decoder.parseBufPacket(buffer, output, offset);
        }

        CHECK(status == DecoderStatus::PacketReady);
        checkFrame(output,
                   static_cast<std::uint16_t>(protocolHeader::CommandType::LOGIN_REQUEST),
                   400,
                   nlohmann::json{{"message", "fragmented"}}.dump());
        CHECK(offset == buffer.size());
    }

    void testInvalidMagic()
    {
        FrameEncoder encoder;
        FrameDecoder decoder;

        std::string packet = encoder.buildBufPacket(
            static_cast<std::uint16_t>(protocolHeader::CommandType::LOGIN_REQUEST),
            500,
            nlohmann::json{{"msg", "bad-magic"}});

        packet[0] = static_cast<char>(0x99);
        packet[1] = static_cast<char>(0x88);
        packet[2] = static_cast<char>(0x77);
        packet[3] = static_cast<char>(0x66);

        size_t offset = 0;
        DecodedFrame output{};
        const DecoderStatus status = decoder.parseBufPacket(packet, output, offset);

        CHECK(status == DecoderStatus::ProtocolError);
        CHECK(offset == 0);
    }

    void testInvalidVersion()
    {
        FrameEncoder encoder;
        FrameDecoder decoder;

        std::string packet = encoder.buildBufPacket(
            static_cast<std::uint16_t>(protocolHeader::CommandType::LOGIN_REQUEST),
            501,
            nlohmann::json{{"msg", "bad-version"}});

        packet[4] = 0;
        packet[5] = 2;

        size_t offset = 0;
        DecodedFrame output{};
        const DecoderStatus status = decoder.parseBufPacket(packet, output, offset);

        CHECK(status == DecoderStatus::ProtocolError);
        CHECK(offset == 0);
    }

    void testBodyTooLarge()
    {
        FrameDecoder decoder;

        protocolHeader::Header header{};
        header.magic = htonl(protocolHeader::MAGIC);
        header.version = htons(protocolHeader::VERSION);
        header.command = htons(static_cast<std::uint16_t>(protocolHeader::CommandType::LOGIN_REQUEST));
        header.body_length = htonl(protocolHeader::MAX_BODY_LENGTH + 1);
        header.request_id = htobe64(502);

        std::string packet;
        packet.append(reinterpret_cast<const char *>(&header), protocolHeader::HEADER_SIZE);

        size_t offset = 0;
        DecodedFrame output{};
        const DecoderStatus status = decoder.parseBufPacket(packet, output, offset);

        CHECK(status == DecoderStatus::ProtocolError);
        CHECK(offset == 0);
    }

    void testZeroLengthBody()
    {
        FrameDecoder decoder;

        const std::string packet = buildRawFrame(
            static_cast<std::uint16_t>(protocolHeader::CommandType::LOGOUT_REQUEST),
            600,
            "");

        size_t offset = 0;
        DecodedFrame output{};
        const DecoderStatus status = decoder.parseBufPacket(packet, output, offset);

        CHECK(status == DecoderStatus::PacketReady);
        CHECK(output.m_command == static_cast<std::uint16_t>(protocolHeader::CommandType::LOGOUT_REQUEST));
        CHECK(output.m_requestId == 600);
        CHECK(output.m_body.empty());
        CHECK(offset == protocolHeader::HEADER_SIZE);
    }

    void testUnknownCommandIsStillAValidFrame()
    {
        FrameDecoder decoder;

        const std::uint16_t unknownButStructurallyValidCommand = 999;
        const std::string packet = buildRawFrame(
            unknownButStructurallyValidCommand,
            700,
            "{}" );

        size_t offset = 0;
        DecodedFrame output{};
        const DecoderStatus status = decoder.parseBufPacket(packet, output, offset);

        CHECK(status == DecoderStatus::PacketReady);
        CHECK(output.m_command == unknownButStructurallyValidCommand);
        CHECK(output.m_requestId == 700);
        CHECK(output.m_body == "{}");
    }

    void testOffsetWithRemainingPartialFrame()
    {
        FrameEncoder encoder;
        FrameDecoder decoder;

        const std::string packetA = encoder.buildBufPacket(
            static_cast<std::uint16_t>(protocolHeader::CommandType::LOGIN_REQUEST),
            800,
            nlohmann::json{{"msg", "A"}});
        const std::string packetB = encoder.buildBufPacket(
            static_cast<std::uint16_t>(protocolHeader::CommandType::REGISTER_REQUEST),
            801,
            nlohmann::json{{"msg", "B"}});

        const std::string buffer = packetA + packetB.substr(0, 7);
        size_t offset = 0;
        DecodedFrame output{};

        DecoderStatus status = decoder.parseBufPacket(buffer, output, offset);
        CHECK(status == DecoderStatus::PacketReady);
        CHECK(output.m_requestId == 800);
        CHECK(offset == packetA.size());

        status = decoder.parseBufPacket(buffer, output, offset);
        CHECK(status == DecoderStatus::NeedMoreData);
        CHECK(offset == packetA.size());
    }

    void runTest(const char *name, void (*test)())
    {
        test();
        std::cout << "[PASS] " << name << '\n';
    }
}

int main()
{
    try
    {
        runTest("EncoderDecoderRoundTrip", testEncoderDecoderRoundTrip);
        runTest("NeedMoreData_PartialHeader", testNeedMoreDataForPartialHeader);
        runTest("NeedMoreData_PartialBody", testNeedMoreDataForPartialBody);
        runTest("TwoFramesInOneBuffer", testTwoFramesInOneBuffer);
        runTest("ThreeFramesInOneBuffer", testThreeFramesInOneBuffer);
        runTest("FragmentedFrame", testFragmentedFrame);
        runTest("InvalidMagic", testInvalidMagic);
        runTest("InvalidVersion", testInvalidVersion);
        runTest("BodyTooLarge", testBodyTooLarge);
        runTest("ZeroLengthBody", testZeroLengthBody);
        runTest("UnknownCommand", testUnknownCommandIsStillAValidFrame);
        runTest("OffsetWithRemainingPartialFrame", testOffsetWithRemainingPartialFrame);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }

    std::cout << "All protocol tests passed.\n";
    return 0;
}
