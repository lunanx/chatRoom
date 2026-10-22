# 指定 CMake 的最低版本
cmake_minimum_required(VERSION 3.21)

# 定义项目名称，并启用 C++
project(ChatRoomClient LANGUAGES CXX)

# 使用 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 启用 Qt 的自动代码处理功能
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 查找 Qt 6 的 各种模块
find_package(Qt6 6.5 REQUIRED COMPONENTS 
    Widgets 
    Network)

# 创建客户端可执行程序
add_executable(ChatRoomClient
    src/main.cpp
    # UI界面
    src/Theme.h

    # 协议编解码
    src/network/ProtocolCodec.h
    src/network/ProtocolCodec.cpp

    # TCP 通信
    src/network/TcpClient.h
    src/network/TcpClient.cpp

    # 主窗口
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp

    # 左侧导航栏及其 QSS 资源
    src/ui/SideBar.h
    src/ui/SideBar.cpp
    src/ui/SideBar.qrc

    # 中间会话列表
    src/ui/SessionListPanel.h
    src/ui/SessionListPanel.cpp
    src/ui/SessionListPanel.qrc

)

# 引用项目公共协议定义和已有的第三方头文件
target_include_directories(ChatRoomClient
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../common"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../resources"
)

# 链接 Qt Widgets 库
target_link_libraries(ChatRoomClient
    PRIVATE 
        Qt6::Widgets
        Qt6::Network
)

# 启用 CTest 测试框架
enable_testing()

# 创建独立的协议编码测试程序
add_executable(ProtocolCodecTest
    tests/ProtocolCodecTest.cpp
    src/network/ProtocolCodec.cpp
    src/network/ProtocolCodec.h
)

# 测试程序同样需要引用公共协议头文件
target_include_directories(ProtocolCodecTest
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../common"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../resources"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# 协议编解码使用 Qt Core 中的基础类型
target_link_libraries(ProtocolCodecTest
    PRIVATE Qt6::Core
)

# 将测试注册到 CTest
add_test(
    NAME ProtocolCodecEncodeTest
    COMMAND ProtocolCodecTest
)