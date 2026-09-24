# SyncCanvas 编码规范

## 强制要求

本文件是 SyncCanvas 仓库的强制编码规范。新增代码、修改代码、生成接口文件、补充注释和重构代码时，都必须遵守本文件；不得以个人习惯替代本文件中的约定。

如果某个子目录存在更深层的 `AGENTS.md`，以更深层文件对该子目录的补充规范为准；没有更深层规范时，统一遵守本文件。修改已有代码时，应尽量保持目标文件原有的局部风格，不要为了格式统一而无关地重排整个文件。

## 项目边界

- `GateServer`：HTTP/TCP 网关，负责客户端请求、验证码和逻辑服务转发。
- `LogicServer`：gRPC 业务服务，负责登录、注册、数据库和 Redis 业务。
- `CanvasServer`、`CanvasServer2`：实时画板、房间和 TCP 会话服务。两套实现中的对应代码应保持接口和风格一致。
- `DBClient`：Qt 客户端。使用 Qt 类型、信号槽和 Qt 的资源管理习惯。
- `VerifyServer`：Node.js CommonJS 验证码服务。
- `message.proto`：跨服务协议的源文件。`message.pb.*` 和 `message.grpc.pb.*` 是生成文件。

## 文件编码与换行

1. 所有源代码、协议、配置、脚本和 Markdown 文档必须使用 UTF-8 编码，统一使用无 BOM UTF-8。
2. 禁止提交 GBK、ANSI、Big5 或其他本地代码页编码的文本文件。中文注释和字符串必须直接保存为 UTF-8，不能依赖编辑器的系统默认编码。
3. C++ 服务的 CMake 已显式配置 `/utf-8` 或 `-finput-charset=UTF-8 -fexec-charset=UTF-8`，不得删除或绕过该配置。
4. Windows 服务启动时必须保留控制台 UTF-8 初始化：`SetConsoleCP(CP_UTF8)` 和 `SetConsoleOutputCP(CP_UTF8)`；Linux 服务使用 UTF-8 locale。
5. 不要为了格式化而混合或批量改动换行符。修改文件时沿用该文件当前的换行风格，并确保提交前仍能按 UTF-8 严格解码。

## C++ 通用规范

### 命名

- 类、结构体、枚举类型、成员函数使用 PascalCase，例如 `LogicSystem`、`GetRoom`、`RegisterUser`。
- 成员变量使用小写下划线命名并以 `_` 开头，例如 `_socket`、`_msg_queue`、`_work_thread`。
- 局部变量、函数参数优先使用小写下划线命名，例如 `server_address`、`room_id`、`request_body`。
- 布尔变量使用能够表达状态的名称，例如 `is_valid`、`has_login`、`get_success`。不要使用无意义的 `flag1`、`temp`。
- 常量、协议前缀和错误码沿用现有的大写或项目既有命名，例如 `CODEPREFIX`、`UID_PREFIX`、`ID_LOGIN_REQ`。新增同类常量必须与所在模块保持一致。
- `message.proto` 中的字段和枚举名称必须保持协议兼容；不要仅为改名而修改已经发布的字段编号。
- 已有公开接口的拼写即使不理想也不要随意改名（例如历史接口中的 `Varify`）；新增接口使用正确且一致的命名。

### 缩进与空白

- 花括号采用换行风格：函数、类、结构体、`if`、`for`、`while`、`try`、`catch` 的 `{` 放在下一行。
- `else`、`catch` 与前一个右花括号同一行，例如 `} else`、`} catch`。
- Lambda 是现有代码中的例外，保持 `[](args) {` 的写法；Lambda 体较长时，体内花括号仍按本文件规则排版。
- `GateServer`、`CanvasServer`、`CanvasServer2` 的旧文件大量使用制表符；修改这些文件时继续使用制表符，不要在同一文件中混入新的空格缩进。
- `LogicServer` 和 `DBClient` 的现有代码主要使用 4 个空格；新建或大段新增代码使用 4 个空格。
- `VerifyServer` 使用 4 个空格。禁止使用 Tab 作为 JavaScript 缩进。
- 运算符、逗号和控制语句关键字周围保留可读空格；不要提交行尾空格。
- 指针和引用贴近类型还是变量，跟随所在文件现有风格；新写 C++ 统一使用 `Type* name`、`Type& name`。

### 头文件与实现文件

- 头文件使用 `#pragma once`，不再新增传统宏 include guard。
- 头文件只包含自身声明真正需要的依赖；能前置声明时不要无条件引入重量级头文件。
- include 顺序保持四组：当前模块头文件、标准库、第三方库、项目其他头文件。已有文件若顺序混乱，只在修改相关区域时顺手整理。
- 类的公开接口放在 `public:`，实现细节放在 `private:`；成员变量放在类的末尾并使用 `_` 前缀。
- 构造函数、析构函数和资源生命周期必须明确。线程、锁、socket、Redis/MySQL 连接等资源应在对应类中成对初始化和释放。
- 单例、管理器和服务类沿用仓库已有的 `getInstance()`、`Inst()` 等接口，不要为同一职责再创建第二套单例模式。
- 不要在头文件中放置会造成多重定义的普通函数或全局变量；需要头文件实现时使用 `inline` 或合适的模板形式。

### 错误处理与并发

- 网络、gRPC、Redis、MySQL 和 JSON 操作失败时，沿用当前模块的错误码和日志格式，不要静默吞掉错误。
- 日志应包含服务名、操作名和必要的标识（如 `uid`、`room_id`），禁止输出密码、Token 等敏感值。
- 访问共享容器、会话、房间和消息队列时必须使用已有互斥量或条件变量；不要绕过管理器直接操作其内部容器。
- 锁的作用域应尽量短，耗时的网络、数据库和序列化操作不要在持锁期间执行，除非已有代码明确要求。
- 新增后台线程必须说明退出条件，并保证析构或停止流程能够回收线程。

## C++ 注释规范

- 注释以中文为主，和现有项目保持一致。协议名、库名、类名、函数名和错误码保留英文原文。
- 注释解释“为什么”以及业务约束，不重复描述显而易见的语句。例如说明“处理最后一个参数对”“停止 IO 线程池”是有价值的，单纯写“定义变量”没有价值。
- 简短说明使用 `//`；类、复杂算法或跨线程流程可在代码前使用连续的 `//` 行说明。
- 行尾注释只用于补充字段、错误码、配置项等短信息；不要让行尾注释过长，也不要在一行堆叠多个无关说明。
- 现有代码中的阶段注释（如 `// 1. Boost`、`// 初始化 Redis`）可以保留；新增分段注释应按模块职责命名。
- 修复乱码时必须把注释本身转换为 UTF-8，不要用问号、替代字符或拼音掩盖无法显示的中文。
- 生成的 Protobuf/gRPC 文件不手工添加业务注释。需要修改协议说明时，修改 `message.proto` 后重新生成对应文件。

## 各子项目补充规范

### GateServer、LogicServer、CanvasServer、CanvasServer2

- 使用 C++14 及项目 CMake 中已配置的依赖和编译选项，不要在单个源文件中偷偷引入新的全局编译开关。
- 服务入口保持“先初始化控制台编码，再初始化配置和单例，最后启动服务”的顺序。
- TCP 消息处理沿用现有的消息 ID、节点、会话和回调注册模式；新增消息必须同时更新协议、生成代码和对应处理函数。
- gRPC 客户端/服务端类按现有 `*GrpcClient`、`*ServiceImpl` 后缀命名，连接失败和 RPC 错误必须记录上下文。
- 房间、会话、线程池和 Redis 管理器的职责不要交叉；新增逻辑优先放入对应管理器或 `LogicSystem`，不要在入口函数中堆积业务代码。

### DBClient

- 使用 Qt 类型和 Qt 生命周期管理；窗口、网络和配置操作优先使用 Qt API。
- 类名使用 PascalCase，槽函数和普通成员函数使用 PascalCase；局部变量使用小写下划线或现有文件已经采用的命名。
- UI 线程中不要执行阻塞的数据库或网络操作；耗时工作应通过现有异步机制处理。
- Qt 信号槽、资源文件和 UI 文件的修改要与对应 C++ 类同步，避免只改一侧造成运行时连接失败。

### VerifyServer

- 使用 CommonJS `require` / `module.exports`，不要在同一模块混用 ES Module 语法。
- 新增变量和函数使用 camelCase；构造函数或类使用 PascalCase；常量使用 UPPER_SNAKE_CASE。已有导出名称必须保持兼容。
- 默认使用 `const`，只有确实需要重新赋值时使用 `let`，不要新增 `var`。
- JavaScript 的花括号与控制语句同行，例如 `if (condition) {`；函数调用和语句使用分号，除非修改区域已经明确采用无分号风格。
- 配置从 `config.json` 或环境变量读取，不要把邮箱密码、Redis 密码和其他运行时凭据写入源码。
- gRPC 协议变更先修改 `message.proto`，再重新生成或更新 `proto.js` 依赖的描述；不要直接编辑生成结果来绕过协议源文件。

### CMake、协议和脚本

- CMake 变量和 target 名称沿用现有项目的 PascalCase/大写风格；新增 target 必须明确 `target_include_directories` 和 `target_link_libraries` 作用域。
- 不要把本机绝对路径、临时构建目录和 IDE 缓存提交到源码配置中。
- `gen_message.bat` 等生成脚本应保持可重复执行；脚本中的路径优先使用相对路径或由 CMake 传入，避免依赖当前工作目录以外的隐式状态。
- `.proto` 文件的字段编号只能追加，不能复用已删除字段的编号；服务端和客户端需要兼容旧消息。

## 修改流程与提交前检查

1. 先确认修改所属子项目，并阅读目标文件附近的实现，沿用其缩进和命名习惯。
2. 涉及协议时先改 `message.proto`，再运行对应生成脚本，检查生成文件没有编码乱码。
3. 修改 C++ 服务后至少执行对应 CMake 配置或构建；修改 VerifyServer 后运行 `npm` 脚本或至少执行 Node.js 语法检查。
4. 提交前确认所有新增和修改的文本文件都是 UTF-8 无 BOM，中文注释没有乱码或替代字符。
5. 执行 `git diff --check`，清理行尾空格、异常空行和无关格式变化。
6. 不要提交 `out/`、`build/`、`.vs/`、`node_modules/`、临时日志、密钥和本机配置。

违反本文件的编码、命名、花括号、注释或文件编码要求的代码，不应视为完成，必须在提交或合并前修正。

# 项目理解（持久化上下文）

本节记录当前源码的架构和实现事实，供后续 AI 会话快速建立上下文。它不是新的编码规范；如果本节与源码不一致，以当前源码、协议文件和配置模板为准。阅读入口优先使用本节列出的文件，再根据具体任务深入实现。

## 1. 项目定位与边界

SyncCanvas 是一个多人协作画板。用户通过 Qt Widgets 客户端注册、登录、进入大厅、创建或加入房间，在同一张画布上同步画笔和几何图形、聊天，并由房主控制普通成员的编辑权限。客户端还支持离线画板、头像上传和房间语音。

项目把低频账号/网关业务、长连接画板业务和实时语音分开：

- HTTP/JSON 负责客户端与 GateServer 之间的账号、验证码、头像和语音 Token 请求。
- gRPC 负责 GateServer、LogicServer、VerifyServer 之间的内部 RPC。
- TCP 长连接负责客户端与 CanvasServer 之间的登录、房间、绘画、聊天和权限消息。
- LiveKit/WebRTC 负责语音媒体，不经过 CanvasServer 的绘画 TCP 链路。

当前实现是学习型分布式单体集合，不是统一的顶层 CMake 工程。每个 C++ 服务和 DBClient 都有自己的 CMakeLists/CMakePresets；服务配置也按目录和 Docker Compose 分开管理。

## 2. 运行拓扑

```text
DBClient (Qt 6)
  ├─ HTTP/JSON ──> GateServer :8080
  │                  ├─ gRPC ──> LogicServer :50058 ──> MySQL + Redis
  │                  └─ gRPC ──> VerifyServer :50057 ──> Redis + SMTP
  ├─ TCP framed ──> CanvasServer :8092 或 CanvasServer2 :8093
  │                  └─ Redis 共享房间元数据，用于跨实例重定向
  ├─ HTTP PUT ───> OSS（头像直传，签名由 GateServer 生成）
  └─ WebSocket/WebRTC ──> LiveKit Cloud（短期 Token 由 VerifyServer 签发）
```

Docker Compose 启动 MySQL、Redis、VerifyServer、LogicServer、GateServer 和两个 CanvasServer 实例。C++ 服务在 Linux 容器中使用 C++14 和静态 vcpkg 依赖构建；DBClient 使用 Qt 6/C++17，并额外依赖 Windows x64 LiveKit C++ SDK。服务间 gRPC 当前使用 insecure credentials，不能把它当作已经部署的 TLS 安全边界。

## 3. 目录与关键入口

### DBClient

- `DBClient/main.cpp`：初始化 Qt、LiveKit、客户端 `config.ini`，创建 `MainWindow`，退出时释放 VoiceManager 和 LiveKit。
- `mainwindow.*`：欢迎页、登录、注册、重置密码、大厅和 Canvas 页面切换。
- `httpmgr.*`：基于 `QNetworkAccessManager` 的异步 HTTP POST/OSS PUT，并按 `Modules` 把结果分发给页面。
- `tcpmgr.*`：`QTcpSocket` 长连接、粘包/半包处理、2 字节消息 ID + 2 字节长度封包、Canvas 登录、房间消息、重定向和断线重连。
- `canvas.*`：在线/离线画板 UI、房间成员列表、聊天、编辑权限、远端绘画缓冲和语音控件。
- `paintscene.*`：本地 `QGraphicsScene` 绘制和远端 `DrawReq` 应用；支持笔、橡皮擦、直线、矩形、椭圆。
- `voicemanager.*`：独立管理 `/voice_token`、LiveKit `Room`、平台音频、麦克风/扬声器开关、设备切换和语音状态。
- `usermgr.*`：当前用户、登录 Token、头像缓存和 `RoomInfo` 相关状态。
- `global.h`：客户端请求 ID、UI 模块 ID、用户/房间结构体。它的 `ReqId` 与服务端 `MSG_IDS`/协议消息 ID 需要一起核对。

### GateServer

- `src/GateServer.cpp`：初始化 UTF-8 控制台、日志、配置和 HTTP `CServer`。
- `src/CServer.cpp`、`src/HttpConnection.cpp`：Boost.Asio/Beast HTTP 接收、路由调用和短连接响应；响应设置 `keep_alive(false)`。
- `src/LogicSystem.cpp`：按 URL 注册 GET/POST handler。当前路由包括 `/get_test`、`/get_verifycode`、`/user_register`、`/reset_password`、`/user_login`、`/voice_token`、`/get_oss_token`、`/save_avator`。
- `LogicGrpcClient`：调用 LogicServer 的注册、登录、重置密码、Token 校验和头像更新 RPC。
- `VerifyGrpcClient`：调用 VerifyServer 的验证码和 LiveKit Token RPC。
- `/get_oss_token` 使用配置中的 OSS 密钥生成 10 分钟 PUT 签名；客户端随后直传 OSS，再通过 `/save_avator` 让 LogicServer 更新 MySQL。

### LogicServer

- `src/LogicServer.cpp`：启动 gRPC 服务，先加载配置，再初始化 `MysqlMgr` 和 `RedisMgr`。
- `src/LogicServiceImpl.cpp`：实现 `RegisterUser`、`ResetPassword`、`Login`、`VerifyToken`、`UpdateAvatar`。
- `MysqlPool`/`MysqlDao`/`MysqlMgr`：MySQL 连接池、预处理 SQL 和账号数据访问。
- 登录会从 MySQL 读取用户，生成随机 Token，并在 Redis 中写入双向索引；同时按 `CanvasServers/Name` 配置随机选择一个 CanvasServer 地址返回给客户端。
- 当前客户端在本地把密码做 SHA-512 hex，服务端 DAO 对提交字符串与 `user.passwd` 做直接比较；修改认证流程时要同时检查 `loginwidget.cpp`、GateServer 和 `MysqlDao.cpp`，不要假设服务端会再次哈希。

### CanvasServer 与 CanvasServer2

- 两个目录是同构的实时画板实例，均有自己的 `CServer`、`CSession`、`LogicSystem`、`SessionMgr`、`RoomMgr`、`Room` 和 `RedisMgr`。修改对应功能时通常需要对照两个目录同步实现。
- `src/CanvasServer.cpp`：初始化 UTF-8、Redis、Asio IO 线程池、业务逻辑线程和 TCP acceptor；CanvasServer2 的日志名和配置端口不同。
- `CSession.cpp`：维护一个 TCP 会话、接收包头/包体、发送队列、登录状态和房间弱引用。发送使用队列串行化 `async_write`，避免同一个 socket 并发写。
- `LogicSystem.cpp`：后台单线程业务队列，处理 Canvas 登录、创建/加入房间、聊天和编辑权限。高频绘画消息不进入该队列。
- `Room.cpp`：成员 Session、房主、可编辑 UID 集合、成员进出广播和进程内绘画历史。
- `RedisMgr.cpp`：共享房间元数据、成员集合和 Token 查找；Redis 只保存跨实例发现所需数据，不保存完整笔迹。

### VerifyServer

- CommonJS Node.js 服务，入口 `VerifyServer/server.js`，通过 `@grpc/grpc-js` 和 `proto-loader` 暴露 `VarifyService`。
- `GetVarifyCode` 从 Redis 读取或生成 6 位验证码，验证码 key 默认 60 秒过期，然后通过 QQ SMTP 发信。
- `CreateVoiceToken` 校验 UID/数字房间号，使用服务端 LiveKit API Key/Secret 生成 5 分钟 JWT，返回 LiveKit URL、房间名和身份。
- `config.js` 读取工作目录下的 `config.json`；启动 VerifyServer 时必须保证当前工作目录使该文件可读。密钥只应存在运行时配置，不应进入客户端或日志。

### Common、configs、docker、docs

- `Common/Logger` 是各 C++ 服务共用的日志实现。
- `configs/prod` 是 Compose 使用的配置文件和 `.example` 模板；`GateServer`/`LogicServer`/`CanvasServer`/`CanvasServer2` 的配置字段必须与各自 `ConfigMgr` 读取的 section 保持一致。
- `docker/cpp.Dockerfile` 一次构建四个 C++ 服务，`docker/verify.Dockerfile` 构建 Node.js 服务。
- `docker/mysql-init/001_schema.sql` 当前主要初始化 `user`、`friend`、`friend_apply` 表；好友表存在，但当前 gRPC/HTTP 业务尚未实现完整好友流程。
- `docs/`、`README.md`、`导学-SyncCanvas.md` 和 `面经-SyncCanvas.md` 是辅助说明；若文字与源码有差异，应以源码和协议为准。

## 4. 关键业务链路

### 注册、验证码和重置密码

1. DBClient 页面向 GateServer 发送 JSON。
2. GateServer 校验 JSON 字段并通过 `VerifyGrpcClient` 请求验证码，或通过 `LogicGrpcClient` 调用注册/重置 RPC。
3. VerifyServer 将 `code_<email>` 写入 Redis 并发邮件；LogicServer 再从同一个 key 校验验证码，成功后删除验证码。
4. LogicServer 通过 MySQL `user` 表完成注册或密码更新，错误码来自协议中的 `message::ErrorCodes`。

### 登录与 Canvas 鉴权

1. DBClient `/user_login` 请求提交邮箱和客户端 SHA-512 hex 密码。
2. GateServer 调用 LogicServer `Login`。
3. LogicServer 校验 MySQL，删除同 UID 的旧 Token，写入 `utoken_<token> -> uid` 和 `uid_token_<uid> -> token`，默认有效期 24 小时，并随机返回一个 CanvasServer 地址。
4. DBClient 用返回的 host/port 建立 TCP 连接，发送 JSON Canvas 登录包：`uid`、`token`、`name`、`avatar`。
5. CanvasServer 直接查 Redis 的 `utoken_<token>` 验证 Token 与 UID 是否匹配，成功后把 UID 绑定到 `CSession` 并加入 `SessionMgr`。

### 创建、加入和跨实例重定向

1. 创建房间请求是 JSON，CanvasServer 生成六位数字房间号，先检查 Redis 冲突，再把名称、房主 UID、实例 host/port、画布宽高写入 Redis，并把房主加入本机 `Room`。
2. 加入房间请求是 Protobuf `JoinRoomReq`。CanvasServer 先读 Redis 房间 hash；如果房间记录的 host/port 不属于当前实例，返回 `NeedRedirect` 和目标地址。
3. DBClient 收到 `NeedRedirect` 后保存待加入的 room/uid，中止旧 socket，连接目标 CanvasServer，重新发送 Canvas 登录，登录成功后自动重发 JoinRoom。
4. 目标实例如果本地没有房间，会根据 Redis 元数据恢复一个内存 `Room`；首次加入者会收到房间历史快照和成员列表。

### 绘画、聊天和权限

- TCP 包头是网络字节序的 2 字节 `msg_id` + 2 字节 `msg_len`，最大包长由服务端 `MAX_LENGTH`（64 KiB）限制。DBClient 的接收缓冲循环处理粘包/半包。
- Canvas 登录和创建房间 body 使用 JSON；加入房间、绘画、聊天、成员、权限等 body 使用 Protobuf。
- `ID_DRAW_REQ` 在 `CSession::ReadBody` 中走快通道：校验已登录、已进房、编辑权限和请求 UID 后直接广播，并把需要的 START/MOVE/END 追加到 `Room` 内存历史；不会进入普通 `LogicSystem` 队列。
- 画笔/橡皮擦 MOVE 点由客户端按定时器批量发送；远端绘画在 Canvas 中进入队列，再按固定节奏应用以平滑公网到达抖动。历史只存在进程内存，进程重启会丢失笔迹。
- 聊天由 `LogicSystem` 校验 Session UID、房间 ID 和内容长度后广播 `ChatRsp`，包含发送者名称、头像和服务端时间戳。
- 房主天然可编辑；普通成员默认只读。只有房主能 Grant/Revoke，权限集合和广播状态在房间内存中，未持久化到 Redis/MySQL。

### 头像与语音

- 头像链路是“GateServer 生成 OSS 签名 -> DBClient PUT 直传 -> `/save_avator` -> LogicServer 更新 MySQL”。`save_avator` 是现有历史拼写，调用方必须保持兼容。
- Canvas JoinRoom 成功后才由 `VoiceManager` 请求 `/voice_token`，请求包含 `uid`、`room_id` 和应用登录 Token。GateServer 先调用 LogicServer `VerifyToken`，再调用 VerifyServer `CreateVoiceToken`。
- DBClient 用返回的短期 Token 连接 LiveKit，`VoiceManager` 在独立连接线程中调用阻塞的 `Room::connect`，在 Qt 线程中完成音频初始化、麦克风发布和 UI 信号分发。Canvas TCP 与 LiveKit 有各自的连接/重连状态。
- 离开房间时释放 LiveKit Room 和音频资源；从 Canvas 暂回大厅时通常调用 `suspendAudio`，回到房间再 `resumeAudio`，以保留语音会话。

## 5. Redis、MySQL 与配置约定

当前源码中最重要的 Redis key 约定如下：

| Key | Value/结构 | 典型生命周期 | 使用方 |
| --- | --- | --- | --- |
| `code_<email>` | 验证码字符串 | 60 秒 | VerifyServer、LogicServer |
| `utoken_<token>` | UID 字符串 | 24 小时 | LogicServer、GateServer Token 校验、CanvasServer |
| `uid_token_<uid>` | 当前 Token | 24 小时 | LogicServer 登录时踢旧会话 |
| `canvas:room:<room_id>` | 房间 hash：name/owner_uid/host/port/width/height | 24 小时 | 两个 CanvasServer |
| `room_users:<room_id>` | UID set | 当前实现由加入逻辑写入 | CanvasServer |

MySQL 当前账号主表是 `user`，字段包含 `id/name/email/passwd/sex/avatar/signature`；`friend` 和 `friend_apply` 是预留关系表。密码字段保存客户端提交的 SHA-512 hex 字符串，修改认证时不要直接把明文密码写入数据库。

配置从可执行文件工作目录读取：C++ 服务通常读取同目录 `config.ini`，VerifyServer 读取同目录 `config.json`，DBClient 读取可执行文件旁的 `config.ini`。生产配置文件可能含数据库、邮箱、OSS 或 LiveKit 凭据，任何代码、日志、文档和提交都不得复制或暴露这些值；新增配置应同时更新对应 `.example` 模板和 Compose 挂载路径。

## 6. 协议文件与生成代码

- 各服务目录都保留一份 `message.proto` 和已生成的 `message.pb.*`、`message.grpc.pb.*`。当前 `GateServer/message.proto` 额外包含 `CreateVoiceToken` RPC 和语音消息；`CanvasServer`、`LogicServer`、`DBClient` 使用的副本没有该 RPC；`VerifyServer/message.proto` 是只包含验证码/语音服务的独立 Node.js 描述。因此修改跨服务协议时必须确认影响范围，不能只改某一份副本。
- C++ 生成文件位于各服务的 `include/...` 与 `src/`，DBClient 的生成文件直接位于 `DBClient/`；VerifyServer 通过 `proto.js` 动态加载 `message.proto`，不手工编辑生成描述。
- `gen_message.bat` 依赖本机 protoc 和 grpc_cpp_plugin 路径，提交前检查生成结果的包名、字段编号和 RPC 方法是否一致。`.proto` 字段编号只能追加，不能复用。
- `ErrorCodes`、TCP 消息 ID、JSON 字段名和历史拼写（例如 `Varify`、`save_avator`）都是运行时契约。新增消息必须同步客户端 handler、CanvasServer 回调和相关协议副本。

## 7. 并发、生命周期与修改边界

- GateServer 使用 Asio IO 线程池处理 HTTP；LogicServer 的 gRPC handler 直接访问管理器；CanvasServer 将普通业务投递到一个带条件变量的 `LogicSystem` 工作线程，高频绘画留在 Session 接收路径。
- `Room`、`SessionMgr`、`RoomMgr`、发送队列和历史均有自己的互斥量或原子状态。需要广播时先复制 Session 快照，再在锁外发送；不要从外部直接操作管理器内部容器。
- Canvas 退出顺序涉及 acceptor、Asio IO 线程池、业务线程、Session 和 Room；新增后台线程必须提供停止条件并在析构时 join。
- CanvasServer 与 CanvasServer2 的网络端口和 `SelfServer` 配置不同，但代码职责相同。`PeerServer`、`RPCPort` 等配置字段目前主要用于配置预留，源码中没有完整的实例间 gRPC 房间同步链路；不要据此假设存在可调用的 Canvas RPC。
- 修改登录/房间协议时优先检查：`message.proto` -> 生成代码 -> `GateServer/src/LogicSystem.cpp` 或 `LogicGrpcClient.cpp` -> `LogicServer/src/LogicServiceImpl.cpp` -> `CanvasServer/src/LogicSystem.cpp`/`CSession.cpp` -> `DBClient/tcpmgr.cpp`/页面 handler。
- 修改绘画时重点检查 `PaintScene` 的本地 item 生命周期、`Canvas` 的节流/远端队列、`CSession` 的快通道权限校验、`Room` 的历史裁切和 CanvasServer/CanvasServer2 的一致性。
- 修改语音时重点检查 `VerifyServer/message.proto`、`VerifyServer/server.js`、GateServer 的 `/voice_token`、DBClient `VoiceManager`，并保证 LiveKit API Secret 只在 VerifyServer 配置中出现。

## 8. 已知实现现状与快速验证

- 当前没有覆盖完整业务的自动化测试套件。C++ 修改至少做对应 CMake configure/build；VerifyServer 修改至少执行 `node --check` 或 npm 脚本；协议修改后要检查所有副本和生成文件。
- 服务入口已按“UTF-8 控制台 -> 配置/单例 -> 启动服务”组织；Windows 下 `SetConsoleCP(CP_UTF8)` 和 `SetConsoleOutputCP(CP_UTF8)` 不能删除。
- HTTP 网关是一次请求一次响应的短连接，内部 gRPC 当前无 TLS；Token、房间元数据和验证码依赖 Redis 可用性。
- 房间历史、编辑权限和本机 Session 不持久化；Redis 重启或 Canvas 进程重启后的房间恢复只能恢复元数据，不能恢复完整画布。
- `CanvasServer2` 的目标程序名仍为 `CanvasServer`，运行目录和 Compose 服务名用于区分实例；排查日志和部署脚本时注意这一点。
- 提交前执行 `git diff --check`，确认新增文档是 UTF-8 无 BOM，且不要把 `build/`、`.vs/`、`node_modules/`、运行日志、生产配置和密钥加入提交。
