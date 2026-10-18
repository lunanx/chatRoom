# 该目录作为子项目由仓库根目录的 CMakeLists.txt 管理。

find_package(Threads REQUIRED)

# 尽早报告开发依赖缺失，避免编译到某个 .cpp 时才发现头文件或链接库不可用。
# 项目内 MariaDB Connector/C++ 文件的根目录
set(MARIADB_CPP_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/mariadb-connector-cpp"
)
# 优先查找项目内的头文件；未找到时继续查找系统安装路径
find_path(MARIADB_CPP_INCLUDE_DIR
    NAMES mariadb/conncpp.hpp
    HINTS
        "${MARIADB_CPP_ROOT}/include"
)
# 优先查找项目内的链接库；未找到时继续查找系统安装路径
find_library(MARIADB_CPP_LIBRARY
    NAMES mariadbcpp
    HINTS
        "${MARIADB_CPP_ROOT}/lib"
        "${MARIADB_CPP_ROOT}/lib64"
)
find_path(SODIUM_INCLUDE_DIR NAMES sodium.h)
find_library(SODIUM_LIBRARY NAMES sodium)

if(NOT MARIADB_CPP_INCLUDE_DIR OR NOT MARIADB_CPP_LIBRARY)
    message(FATAL_ERROR
        "找不到 MariaDB Connector/C++。请确认已安装 mariadb/conncpp.hpp 头文件和 libmariadbcpp 库。")
endif()

if(NOT SODIUM_INCLUDE_DIR OR NOT SODIUM_LIBRARY)
    message(FATAL_ERROR
        "找不到 libsodium 开发文件。请确认已安装 sodium.h 头文件和 libsodium 链接库。")
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
    src/model/Conversation.cpp
    src/model/Conversation.h

    src/service/UserService.cpp
    src/service/UserService.h
    src/service/FriendService.cpp
    src/service/FriendService.h
    src/service/MessageService.cpp
    src/service/MessageService.h
    src/service/ConversationService.cpp
    src/service/ConversationService.h

    src/repository/UserRepository.cpp
    src/repository/UserRepository.h
    src/repository/MessageRepository.cpp
    src/repository/MessageRepository.h
    src/repository/ConversationRepository.cpp
    src/repository/ConversationRepository.h

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

# 默认配置路径由 CMake 注入；环境变量 CHATROOM_DB_CONFIG / CHATROOM_SERVER_CONFIG 可在运行时覆盖。
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

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(InternetChatRoomServer PRIVATE -Wall -Wextra -Wpedantic)
endif()
