cmake_minimum_required(VERSION 3.16)

project(InternetChatRoomServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

# 把外部库缺失的问题提前到 CMake 配置阶段报告，而不是等到编译某个 .cpp 时才发现头文件缺失。
find_path(MARIADB_CPP_INCLUDE_DIR NAMES mariadb/conncpp.hpp)
find_library(MARIADB_CPP_LIBRARY NAMES mariadbcpp)
find_path(SODIUM_INCLUDE_DIR NAMES sodium.h)
find_library(SODIUM_LIBRARY NAMES sodium)

if(NOT MARIADB_CPP_INCLUDE_DIR OR NOT MARIADB_CPP_LIBRARY)
    message(FATAL_ERROR
        "找不到 MariaDB Connector/C++。请确认已安装 conncpp.hpp 头文件和 libmariadbcpp 库。")
endif()

if(NOT SODIUM_INCLUDE_DIR OR NOT SODIUM_LIBRARY)
    message(FATAL_ERROR
        "找不到 libsodium 开发文件。请安装 sodium.h 头文件和 libsodium 链接库。")
endif()

add_executable(InternetChatRoomServer
    src/main.cpp

    src/net/TcpServer.cpp
    src/net/TcpServer.h
    src/net/ServerConfig.cpp
    src/net/ServerConfig.h
    src/net/EpollReactor.cpp
    src/net/EpollReactor.h
    src/net/ClientSession.cpp
    src/net/ClientSession.h

    src/db/DatabaseManager.cpp
    src/db/DatabaseManager.h
    src/db/DatabaseConfig.cpp
    src/db/DatabaseConfig.h

    src/model/User.cpp
    src/model/User.h
    src/model/Message.cpp
    src/model/Message.h

    src/service/UserService.cpp
    src/service/UserService.h
    src/service/FriendService.cpp
    src/service/FriendService.h
    src/service/MessageService.cpp
    src/service/MessageService.h

    src/repository/UserRepository.cpp
    src/repository/UserRepository.h
    src/repository/MessageRepository.cpp
    src/repository/MessageRepository.h

    src/protocol/FrameDecoder.cpp
    src/protocol/FrameDecoder.h
    src/protocol/FrameEncoder.cpp
    src/protocol/FrameEncoder.h

    src/dispatcher/Dispatcher.cpp
    src/dispatcher/Dispatcher.h
    src/security/PasswordHasher.cpp
    src/security/PasswordHasher.h
)

target_include_directories(InternetChatRoomServer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources
        ${MARIADB_CPP_INCLUDE_DIR}
        ${SODIUM_INCLUDE_DIR}
)

# 程序默认从仓库根目录的 config/database.conf 读取数据库配置。
# 也可以运行时设置 CHATROOM_DB_CONFIG 环境变量覆盖此路径。
target_compile_definitions(InternetChatRoomServer
    PRIVATE
        CHATROOM_DB_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../config/database.conf"
        CHATROOM_SERVER_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../config/server.conf"
)

target_link_libraries(InternetChatRoomServer
    PRIVATE
        Threads::Threads
        ${SODIUM_LIBRARY}
        ${MARIADB_CPP_LIBRARY}
)

# GCC/Clang 下启用常见警告，帮助发现类型和潜在逻辑问题。
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(InternetChatRoomServer PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 协议单元测试独立于服务器运行，不需要连接数据库。
if(BUILD_TESTING)
    add_executable(ProtocolUnitTest
        tests/protocol_test.cpp
        src/protocol/FrameDecoder.cpp
        src/protocol/FrameEncoder.cpp
    )

    target_include_directories(ProtocolUnitTest
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/../common
            ${CMAKE_CURRENT_SOURCE_DIR}/../resources
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ProtocolUnitTest PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME ProtocolUnitTest COMMAND ProtocolUnitTest)
endif()
