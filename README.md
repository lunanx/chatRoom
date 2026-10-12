# chatRoom

let us chat!

## 服务端构建

本项目使用仓库根目录的 `CMakeLists.txt` 管理 Linux 服务端和协议单元测试。服务端需要 C++17 编译器、CMake、MariaDB Connector/C++ 开发文件，以及 libsodium 开发文件。

首次配置数据库前，复制数据库配置模板并填写数据库连接信息：

```bash
cp config/database.example.conf config/database.conf
```

`config/database.conf` 包含数据库密码，已被 `.gitignore` 忽略，不要提交到 Git。服务端监听配置位于 `config/server.conf`，它和数据库配置相互独立。默认监听 `0.0.0.0:8888`，表示监听本机所有 IPv4 网卡；客户端应连接服务器实际可达的 IP 地址。

在仓库根目录执行完整配置与构建：

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

服务端可执行文件输出在 `build/server/InternetChatRoomServer`。默认配置路径由 CMake 编译宏确定，因此从其他工作目录启动程序也不会因为相对路径变化而找不到配置。运行时也可以分别使用 `CHATROOM_DB_CONFIG` 和 `CHATROOM_SERVER_CONFIG` 环境变量覆盖对应配置文件路径。

### 只构建协议测试

协议单元测试不需要连接数据库，也不依赖 MariaDB Connector/C++ 或 libsodium。如果当前开发环境还没安装服务端依赖，可以先执行：

```bash
cmake -S . -B build-protocol-only \
  -DCHATROOM_BUILD_SERVER=OFF \
  -DCHATROOM_BUILD_PROTOCOL_TESTS=ON \
  -DBUILD_TESTING=ON
cmake --build build-protocol-only -j
ctest --test-dir build-protocol-only --output-on-failure
```

### TCP 联调测试

`server/tests/tcp_protocol_test.py` 是需要服务端先启动后的手工冒烟测试。它使用故意不符合长度要求的登录请求，以免依赖数据库账号；可通过 `--host` 和 `--port` 指定服务器地址，例如：

```bash
python3 server/tests/tcp_protocol_test.py --host 127.0.0.1 --port 8888
```

