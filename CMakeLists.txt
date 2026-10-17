cmake_minimum_required(VERSION 3.16)

project(ChatRoom LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

option(CHATROOM_BUILD_SERVER "Build the Linux TCP chat server" ON)
option(CHATROOM_BUILD_PROTOCOL_TESTS "Build protocol unit tests" ${BUILD_TESTING})

if(CHATROOM_BUILD_SERVER)
    add_subdirectory(server)
endif()

# 协议测试不依赖 MariaDB 或 libsodium，可以单独构建和运行。
if(BUILD_TESTING AND CHATROOM_BUILD_PROTOCOL_TESTS)
    add_executable(ProtocolUnitTest
        server/tests/protocol_test.cpp
        server/src/protocol/FrameDecoder.cpp
        server/src/protocol/FrameEncoder.cpp
    )

    target_include_directories(ProtocolUnitTest
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/server/src
            ${CMAKE_CURRENT_SOURCE_DIR}/common
            ${CMAKE_CURRENT_SOURCE_DIR}/resources
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ProtocolUnitTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME ProtocolUnitTest COMMAND ProtocolUnitTest)
endif()
