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
)

target_include_directories(InternetChatRoomServer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(InternetChatRoomServer
    PRIVATE
        Threads::Threads
)