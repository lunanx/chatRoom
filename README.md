# chatRoom

let us chat!

## 服务端构建

本项目使用仓库根目录的 `CMakeLists.txt` 管理服务端和协议测试。需要 C++17 编译器、CMake、MariaDB Connector/C++ 开发文件，以及 libsodium 开发文件。

首次配置数据库前，复制配置模板并填写数据库连接信息：

```bash
cp config/database.example.conf config/database.conf
```

`config/database.conf` 包含数据库密码，已被 `.gitignore` 忽略，不要提交到 Git。服务端监听配置位于 `config/server.conf`，它和数据库配置相互独立。默认监听 `0.0.0.0:8888`，表示监听本机所有 IPv4 网卡；客户端连接时应使用服务器实际可达的 IP 地址。

在仓库根目录执行：

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

服务端可执行文件输出在 `build/server/InternetChatRoomServer`。数据库和服务端配置文件的默认路径由 CMake 设置，因此从其他工作目录启动程序也不会因相对路径变化而找不到配置。运行时也可以分别用 `CHATROOM_DB_CONFIG` 和 `CHATROOM_SERVER_CONFIG` 环境变量覆盖对应配置文件路径。

