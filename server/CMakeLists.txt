cmake_minimum_required(VERSION 3.16)

project(InternetChatRoomServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

add_executable(InternetChatRoomServer

    src/main.cpp

    src/net/TcpServer.cpp
    src/net/TcpServer.h

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
    src/protocol/FrameEncoder.h
    src/protocol/FrameEncoder.cpp

    src/dispatcher/Dispatcher.h
    src/dispatcher/Dispatcher.cpp
    src/security/PasswordHasher.h
    src/security/PasswordHasher.cpp
)

target_include_directories(InternetChatRoomServer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../resources
)

target_link_libraries(InternetChatRoomServer
    PRIVATE
        Threads::Threads
        sodium
        mariadbcpp
)
# 开启编译警告  方便进一步学习C++
target_compile_options(InternetChatRoomServer 
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)


# Protocol tests: 独立测试目标，不加入生产服务器可执行文件
option(BUILD_PROTOCOL_TESTS "Build protocol unit tests" ON)

if(BUILD_PROTOCOL_TESTS)
    enable_testing()

    add_executable(ProtocolUnitTest
        tests/test_database_manager.cpp
        src/db/DatabaseManager.cpp
        src/db/DatabaseConfig.cpp
        src/db/DatabaseManager.h
        src/db/DatabaseConfig.h

    )

    target_include_directories(ProtocolUnitTest
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/../common
            ${CMAKE_CURRENT_SOURCE_DIR}/../resources
    )

    target_link_libraries(ProtocolUnitTest
    PRIVATE
        Threads::Threads
        sodium
        mariadbcpp
    )

    target_compile_options(ProtocolUnitTest
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )

    add_test(NAME ProtocolUnitTest COMMAND ProtocolUnitTest)
endif()
