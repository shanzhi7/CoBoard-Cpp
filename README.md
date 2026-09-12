# SyncCanvas

SyncCanvas 是一个基于 Qt 的多人协作画板。用户可以注册并登录账号、创建或加入房间，在同一张画布上实时绘制、聊天；房主还可以控制成员的编辑权限。项目采用“Qt 客户端 + C++ 多服务 + Node.js 验证服务”的分布式结构，适合学习 Qt 网络编程、Boost.Asio、Protobuf/gRPC、Redis 和 MySQL 的组合使用。

> 文档根据当前源码整理。仓库中的协议文件、生成代码和配置模板属于实现的一部分，修改协议时需要同步更新各服务目录。

## 功能概览

### 客户端

- 欢迎页、登录、注册、重置密码和大厅页面。
- 离线画板：无需连接服务端即可绘制，并支持本地图元撤销。
- 在线房间：创建房间、按房间号加入房间、返回最近进入的房间。
- 画笔、橡皮擦、直线、矩形、椭圆；可调整颜色和线宽，并显示鼠标坐标。
- 房间成员列表、成员加入/离开提示和房间群聊。
- 房主授权或取消成员的编辑权限；普通成员默认只读。
- 头像选择、OSS 签名上传和头像地址保存。
- TCP 断线检测、指数退避重连；重连后自动重新进行 Canvas 登录并尝试回到原房间。
- LiveKit Cloud 房间语音：进入在线房间后自动获取短期 Token、连接语音房间并发布麦克风；离开房间时自动断开。
- 语音支持麦克风开关、听筒开关、连接状态和活动说话人事件，UI 控件可按需接入。

### 服务端

- GateServer：对客户端提供 HTTP/JSON 网关，处理验证码、注册、登录、重置密码、OSS 签名和头像保存等请求。
- LogicServer：通过 gRPC 提供账号业务，访问 MySQL 用户数据和 Redis 验证码/Token，并为登录请求选择 CanvasServer。
- VerifyServer：Node.js gRPC 服务，生成验证码、写入 Redis 并发送邮件。
- CanvasServer：Boost.Asio TCP 长连接服务，负责会话、房间、绘画广播、聊天、权限和房间历史回放。
- CanvasServer2：与 CanvasServer 同构的第二个实例，用于多实例房间重定向场景。

## 架构

```text
                         +------------------+
                         |   MySQL 8        |
                         | 用户、好友表结构  |
                         +---------+--------+
                                   |
+------------+   HTTP/JSON   +-----v------+     gRPC      +--------------+
| DBClient   +--------------->| GateServer +-------------->| LogicServer  |
| Qt 6       |                +-----+------+               +------+-------+
+-----+------+                      |                             |
      | TCP（JSON/Protobuf）        | gRPC                        +-----> Redis
      |                             v                                   (Token、验证码、房间元数据)
      |                       +-----------+
      +---------------------->| Verify    |
                              | Server    |----> SMTP 邮箱
                              +-----------+

      TCP 长连接（JSON/Protobuf）
      +----------------------+----------------------+
      |                                             |
+-----v--------+                             +------v-------+
| CanvasServer |<---------- Redis ---------->| CanvasServer2|
| :8092        |                             | :8093        |
+--------------+                             +--------------+

DBClient 上传头像时，先从 GateServer 获取 OSS 签名，再直接上传对象存储，最后回写头像地址。
```

## 目录说明

| 目录 | 作用 |
| --- | --- |
| `DBClient/` | Qt Widgets 客户端、UI 文件、绘制场景、HTTP/TCP 管理器和 Protobuf 代码 |
| `GateServer/` | Boost.Beast HTTP 网关及到 Logic/Verify 的 gRPC 客户端 |
| `LogicServer/` | gRPC 业务服务、MySQL 连接池/DAO、Redis Token 管理 |
| `CanvasServer/` | 画板 TCP 服务、会话/房间管理和实时广播 |
| `CanvasServer2/` | CanvasServer 的第二实例，配置和端口独立 |
| `VerifyServer/` | Node.js 验证码 gRPC 服务和邮件发送逻辑 |
| `docker/` | C++、Node.js 镜像构建文件和 MySQL 初始化脚本 |
| `configs/` | Compose 使用的服务配置；部署前请替换为自己的值 |
| `docs/` | 设计记录和功能建议 |

客户端和服务端的关键入口：

- `DBClient/main.cpp`：读取客户端旁的 `config.ini` 并启动主窗口。
- `DBClient/tcpmgr.cpp`：TCP 拆包、发包、CanvasServer 重定向和重连。
- `DBClient/canvas.cpp`、`DBClient/paintscene.cpp`：画布界面、本地绘制和远端图元应用。
- `GateServer/src/LogicSystem.cpp`：HTTP 路由实现。
- `LogicServer/src/LogicServiceImpl.cpp`：注册、登录、重置密码和头像更新。
- `CanvasServer/src/CSession.cpp`、`CanvasServer/src/Room.cpp`：TCP 会话、绘画快通道、广播和历史回放。
- `DBClient/voicemanager.cpp`：LiveKit Token 请求、房间连接、麦克风发布和语音状态管理。

## 通信与数据流

### 客户端登录

1. DBClient 向 GateServer 发送 HTTP/JSON 登录请求。
2. GateServer 通过 gRPC 调用 LogicServer。
3. LogicServer 校验 MySQL 中的账号密码，在 Redis 中保存 24 小时 Token，并返回一个 CanvasServer 地址。
4. DBClient 连接该 CanvasServer，发送 `uid + token` 完成 Canvas 登录鉴权。

GateServer 当前注册的 HTTP 路由如下：

| 方法 | 路径 | 用途 |
| --- | --- | --- |
| `GET` | `/get_test` | 基础连通性测试 |
| `POST` | `/get_verifycode` | 请求邮箱验证码 |
| `POST` | `/user_register` | 注册账号 |
| `POST` | `/reset_password` | 重置密码 |
| `POST` | `/user_login` | 登录并获取 Token/CanvasServer 地址 |
| `POST` | `/voice_token` | 校验客户端登录 Token 并获取 LiveKit 短期语音 Token |
| `POST` | `/get_oss_token` | 获取头像上传签名 |
| `POST` | `/save_avator` | 保存头像公开地址（路径名称与源码保持一致） |

### 创建和加入房间

1. 创建房间时，CanvasServer 生成六位房间号，将房间名称、房主、画布宽高和所属实例写入 Redis（房间元数据默认 24 小时过期）。
2. 加入房间时，服务端从 Redis 查找房间归属。如果房间属于另一实例，返回 `NeedRedirect`、目标 host/port；客户端自动切换连接并重试加入。
3. 首次加入内存中的房间时，服务端会把当前历史操作回放给新成员，并广播成员加入/离开事件。

### 房间语音

1. DBClient 加入或创建在线房间成功后，向 GateServer 的 `/voice_token` 发送 `uid`、`room_id` 和当前登录 `app_token`。
2. GateServer 先调用 LogicServer 校验登录 Token 和 UID，再调用 VerifyServer 生成 LiveKit Cloud 短期 Token。
3. VerifyServer 使用服务端保存的 LiveKit API Key/API Secret 生成 Token，客户端只接收 LiveKit URL、房间名、身份和短期 Token。
4. DBClient 使用 LiveKit C++ SDK 连接语音房间，`PlatformAudio` 负责系统麦克风采集、远端音频播放以及 WebRTC 回声消除、降噪和自动增益。
5. 离开画板房间、切换离线模式或客户端退出时，VoiceManager 会断开 LiveKit 房间并释放音频资源。

```text
DBClient --HTTP /voice_token--> GateServer --gRPC VerifyToken--> LogicServer
    |                                  |
    |                                  +--gRPC CreateVoiceToken--> VerifyServer
    |
    +---------- WebSocket/WebRTC + 短期 Token ----------> LiveKit Cloud
```

LiveKit API Key 和 API Secret 只配置在 VerifyServer，禁止写入 DBClient、GateServer 或公开配置模板。

### 绘画同步

CanvasServer 的 TCP 包格式为：

```text
2 字节 message_id（网络字节序）
2 字节 message_len（网络字节序）
message_len 字节 message body
```

包体编码取决于消息类型：Canvas 登录和创建房间使用 JSON；加入房间、绘画、聊天、编辑权限和成员广播使用 Protobuf。`DrawReq` 使用 `CMD_START`、`CMD_MOVE`、`CMD_END` 表示一笔操作的生命周期。画笔和橡皮擦的移动点由客户端按约 16 ms 节奏批量发送，每包最多 80 个点；直线、矩形和椭圆的移动阶段用于实时预览。绘画请求绕过普通业务队列，由会话层校验登录状态、房间归属、编辑权限以及请求 UID 后广播给其他成员。

### 权限与历史

- 房主始终可编辑，普通成员加入后默认只读。
- 只有房主可以发送授权/取消授权请求；权限变化通过广播同步到客户端。
- 房间历史目前保存在 CanvasServer 进程内存中，新成员可以回放；Redis 只保存房间元数据和成员集合，不保存完整笔迹。

## 构建环境

### 通用依赖

- CMake 3.20 或更高版本
- C++14 编译器；Windows 可使用 Visual Studio 2022，Linux 可使用 GCC/Clang
- Ninja（推荐）
- vcpkg 依赖：Boost（Asio、Beast、Filesystem、System、UUID）、gRPC、Protobuf、JsonCpp、hiredis、redis-plus-plus、MySQL Connector/C++、libmysql、OpenSSL、Zlib、lz4、zstd、abseil
- MySQL 8 和 Redis 7

### 客户端依赖

- Qt 6.5（Core、Widgets、Network）
- 与服务端一致的 Protobuf/gRPC C++ 库
- Qt 6.5.3 MSVC x64 客户端还需要 LiveKit C++ SDK 1.10.x Windows x64 预编译包。

### 验证码服务依赖

- Node.js 18 或更高版本
- npm
- 可用的 SMTP 邮箱；在线注册/重置密码时需要发送验证码

## 快速启动（Docker Compose）

Compose 会启动 MySQL、Redis、VerifyServer、LogicServer、GateServer 和两个 CanvasServer。先准备环境变量文件（不要提交到 Git）：

```dotenv
MYSQL_ROOT_PASSWORD=请设置 MySQL 密码
REDIS_PASSWORD=请设置 Redis 密码
DOCKERHUB_USER=你的镜像仓库用户名
```

然后检查并修改以下挂载配置中的地址、密码、邮箱和 OSS 参数：

- `configs/prod/GateServer.config.ini`
- `configs/prod/LogicServer.config.ini`
- `configs/prod/CanvasServer.config.ini`
- `configs/prod/CanvasServer2.config.ini`
- `configs/prod/VerifyServer.config.json`

启动服务：

```bash
docker compose build
docker compose up -d
docker compose ps
```

默认端口如下：

| 服务 | 容器内/对外端口 | 说明 |
| --- | --- | --- |
| GateServer | `8080` | 客户端 HTTP 网关 |
| CanvasServer | `8092` | 第一个画板 TCP 实例 |
| CanvasServer2 | `8093` | 第二个画板 TCP 实例 |
| LogicServer | `50058` | 仅 Compose 网络内访问的 gRPC 服务 |
| VerifyServer | `50057` | 仅 Compose 网络内访问的 gRPC 服务 |
| MySQL | `3306` | Compose 网络内使用；数据持久化到 `mysql-data` |
| Redis | `6379` | Compose 网络内使用；数据持久化到 `redis-data` |

MySQL 首次启动会执行 `docker/mysql-init/001_schema.sql`。Compose 配置把 CanvasServer 的 `SelfServer.Host` 写入房间元数据，部署到其他主机或云环境时必须改成客户端可访问的地址，不能直接照搬示例中的局域网 IP。

## 源码构建

项目没有根目录统一 CMake，每个 C++ 模块独立构建。以 Linux + vcpkg 为例：

```bash
cmake -S GateServer -B GateServer/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux
cmake --build GateServer/build
```

对 `LogicServer`、`CanvasServer`、`CanvasServer2` 重复上述命令即可。Windows 可以使用各目录的 `CMakePresets.json`，或在 Qt Creator/Visual Studio 中打开对应目录。

每个服务启动时都会从“当前工作目录”读取 `config.ini`，可复制对应的 `config.ini.example` 后填写实际配置。服务端建议按以下顺序启动：

```text
MySQL、Redis -> VerifyServer -> LogicServer -> GateServer
             -> CanvasServer / CanvasServer2 -> DBClient
```

### 构建客户端

```bash
cmake -S DBClient -B DBClient/build -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.5.x \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux
cmake --build DBClient/build
```

Windows 下建议使用 Qt 6.5.3 MSVC x64 Kit 和 Visual Studio 2022。DBClient 的 CMake 需要能够找到 LiveKit SDK 的 `include/`、`lib/` 和 `bin/` 目录；SDK 根目录只用于本机构建，不需要提交到仓库。

Release 构建完成后，使用 Qt 官方工具部署 Qt 依赖，并将以下两个 LiveKit 运行时 DLL 复制到 `DBClient.exe` 同目录：

```text
livekit.dll
livekit_ffi.dll
```

最终将包含 `DBClient.exe`、Qt DLL、`platforms/qwindows.dll`、上述 LiveKit DLL 和客户端 `config.ini` 的目录压缩发布即可。发布包不需要携带 LiveKit SDK 的头文件、`.lib` 文件或 API Secret。

运行 `DBClient` 前，在可执行文件同目录放置 `config.ini`：

```ini
[GateServer]
host = 127.0.0.1
port = 8080
```

### 启动 VerifyServer

```bash
cd VerifyServer
npm install
# 根据 config.json 配置 SMTP 和 Redis
npm run serve
```

## 配置与安全

- `config.ini`、`config.json` 和 `.env` 可能包含数据库密码、Redis 密码、邮箱授权码、OSS 密钥，生产环境应使用独立的密钥管理或运行时挂载，禁止把真实凭据提交到仓库。
- LiveKit API Key/API Secret 只允许出现在 VerifyServer 的运行时 `config.json` 中；仓库只保留 `VerifyServer/config.json.example` 和 `configs/prod/VerifyServer.config.json.example`。
- DBClient 只保存登录 Token 和 LiveKit 短期 Token，不应保存 LiveKit API Key/API Secret。
- `config.ini`、`config.json`、`.env`、构建目录、Qt Creator `.user` 文件、`node_modules` 和发布压缩包均不应加入 Git 跟踪；对应的脱敏模板可以提交。
- GateServer 当前使用 HTTP，内部 gRPC 默认使用不带 TLS 的连接；部署到公网前应增加 HTTPS/TLS、访问控制和反向代理。
- OSS 上传需要配置 `AliyunOSS` 的密钥、Bucket、Endpoint 和 Host；不需要头像功能时可以关闭对应入口。
- 修改任一 `message.proto` 后，应使用项目中的 `gen_message.bat` 或 `protoc`/gRPC 工具重新生成各目录下的 `.pb.*` 文件，并确保所有模块使用同一份协议。

## 已知限制

- 联机撤销和清屏的协议字段已定义，但当前没有完整的服务端处理和广播链路；可用的撤销仅限离线画板中的本地图元。
- 绘画历史只存在于 CanvasServer 内存，服务进程重启后不会恢复完整笔迹；Redis 中仅保留房间元数据和成员集合。
- 历史回放与实时绘画尚未使用全局序列号，多人同时加入并绘制时只能提供尽力而为的一致性。
- 数据库中已有好友相关表结构，但当前客户端和服务端主流程未提供完整的好友业务界面/API。
- `CanvasServer2` 是用于多实例测试的同构服务，不是独立的业务版本；两个实例需要正确配置可互相访问的地址和端口。
- 部分旧源码注释存在编码不一致，阅读时建议统一按 UTF-8 处理。

## 截图

![画板界面](画板效果图.png)

![服务端设计](服务端设计.png)
