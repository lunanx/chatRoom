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


### 注册/登录业务异常分支测试

`server/tests/business_error_branch_test.py` 会针对当前协议验证以下场景：重复注册、注册缺少 `nickname`、非法 JSON、用户名格式错误、过短密码、错误密码登录、不存在的用户名、登录缺少 `password`、错误密码退出登录，以及正确登录/退出登录。脚本会创建一个自动生成的测试账号，不会自动删除数据库记录。

确认服务端和 MariaDB 正常运行后，在项目根目录执行：

```bash
python3 server/tests/business_error_branch_test.py
```

如果也想检查数据库中的 `User.online` 字段，可以添加 `--verify-db-state`。该模式会调用 `mysql` 或 `mariadb` 命令行客户端读取 `config/database.conf` 并检查注册后为 `0`、登录成功后为 `1`、错误密码退出后仍为 `1`、正常退出后回到 `0`：

```bash
python3 server/tests/business_error_branch_test.py --verify-db-state
```

#### 单独验证数据库连接失败分支

这个模式不会自行停止 MariaDB，也不会改动数据库。最安全的验证方式是先按 `Ctrl+C` 停掉当前服务端进程，复制一份配置并将数据库端口改成一个没有服务监听的端口（例如 `1`）：

```bash
cp config/database.conf /tmp/chatroom-db-unavailable.conf
sed -i 's/^DB_PORT=.*/DB_PORT=1/' /tmp/chatroom-db-unavailable.conf
chmod 600 /tmp/chatroom-db-unavailable.conf
CHATROOM_DB_CONFIG=/tmp/chatroom-db-unavailable.conf ./build/server/InternetChatRoomServer
```

保持这个服务端运行，再开一个终端执行：

```bash
python3 server/tests/business_error_branch_test.py --database-unavailable-only
```

预期注册和登录都应返回相应失败命令，`message` 包含“服务器暂时无法处理”，而不是误报成功或把数据库故障说成“用户名不存在”。测试完成后按 `Ctrl+C` 关闭这个使用临时错误配置的服务端，再正常启动 `./build/server/InternetChatRoomServer`。`/tmp/chatroom-db-unavailable.conf` 仅用于故障测试，不要覆盖正式的 `config/database.conf`。

