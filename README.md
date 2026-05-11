# SyncCanvas

多人协作画板项目。前端使用 Qt 6.5.3、QGraphicsScene、QTcpSocket、Protobuf，后端使用 Boost.Asio、Boost.Beast、gRPC、JsonCpp、MySQL、Redis、Node.js 等。

## 当前功能状态（2026-05-11）

1. 客户端
   - 支持欢迎页、登录、注册、重置密码、大厅、画布窗口。
   - 支持离线画板模式，可直接进入本地画布绘制。
   - 支持画笔、矩形、椭圆、直线、橡皮擦。
   - 支持画笔颜色、线宽选择、鼠标坐标显示、画布尺寸设置。
   - 离线模式支持本地图元撤销；联机撤销协议字段已预留，服务端广播链路尚未落地。

2. 账号与用户信息
   - GateServer 提供 HTTP 接口，负责注册、登录、重置密码、验证码、头像上传签名等入口。
   - VerifyServer 通过 gRPC 提供邮箱验证码服务，并将验证码写入 Redis。
   - LogicServer 负责账号校验、Token 生成、MySQL 数据访问、Redis Token 存储。
   - 客户端支持头像上传到 OSS，并通过 GateServer/LogicServer 保存头像地址。

3. 房间系统
   - 支持创建房间、加入房间、返回已有房间。
   - 支持自定义画布尺寸，并由服务端返回给客户端设置场景大小。
   - 支持多 CanvasServer 实例下的 NeedRedirect 自动切服。
   - 支持成员加入、离开广播，在线用户列表实时更新。
   - 支持房间懒加载：CanvasServer 重启后，如果 Redis 中存在 room_info，JoinRoom 会按需恢复 Room 元信息。

4. 实时绘画同步
   - 使用 `DrawReq` 表示绘画操作流，按 START、MOVE、END 三阶段同步。
   - Pen/Eraser 使用 `path_points` 做增量点同步。
   - 矩形、椭圆、直线在 MOVE 阶段实时预览。
   - CanvasServer 对 `ID_DRAW_REQ` 走快通道处理，不进入 LogicQueue，降低高频绘画数据的排队延迟。
   - 服务端校验登录状态、房间状态、编辑权限和请求 UID，防止未登录或伪造 UID 的绘画请求。

5. 历史回放与恢复
   - CanvasServer 在 Room 内维护内存级 `_history`，保存可回放的绘画操作。
   - 新用户第一次加入房间时，服务端会先向该用户回放已有绘画历史。
   - 断线重连后，客户端自动 CanvasLogin 并重新 Join 上次房间，配合内存历史恢复画布状态。
   - 当前绘画历史没有持久化到 Redis 或 MySQL，CanvasServer 进程重启后只能恢复房间元信息，不能恢复完整笔迹。

6. 协作权限与聊天
   - 默认房主可编辑画板，普通成员进入房间后为只读。
   - 房主可以在成员列表中授权或取消成员编辑权限。
   - 服务端维护授权集合，并通过 `PermissionChangedBroadcast` 同步权限变化。
   - 支持房间群聊，客户端发送 `ChatReq`，服务端校验房间和 UID 后广播 `ChatRsp`。

7. 网络与稳定性
   - 客户端 TCP 包头为 `quint16 message_id + quint16 message_len`，使用 BigEndian。
   - 客户端支持断线检测和指数退避重连。
   - 重连成功后自动进行 CanvasLogin、JoinRoom，且支持重连过程中再次触发 NeedRedirect。
   - 服务端发送链路使用发送队列，并通过 socket executor 串行化 `async_write`，避免同一 socket 并发写导致的不稳定问题。
   - 画笔和橡皮擦 MOVE 数据使用 16ms 定时器批量发送，并限制单包最大点数，避免瞬时大包。

## 系统架构

```text
Qt DBClient
  | HTTP: 注册、登录、验证码、头像
  v
GateServer
  | gRPC
  +--> VerifyServer: 邮箱验证码
  |
  +--> LogicServer: 用户、密码、Token、CanvasServer 路由
          |
          +--> MySQL: 用户数据
          +--> Redis: 验证码、Token、房间元信息

Qt DBClient
  | TCP + Protobuf: CanvasLogin、CreateRoom、JoinRoom、Draw、Chat
  v
CanvasServer / CanvasServer2
  +--> RoomMgr / Room / SessionMgr
  +--> Redis: 房间元信息、房间成员集合
```

## 主要模块

### DBClient

Qt 客户端，负责界面、登录流程、房间操作、本地绘制和远端渲染。

关键文件：

1. `DBClient/main.cpp`：读取 `config.ini`，初始化 GateServer 地址并启动主窗口。
2. `DBClient/mainwindow.cpp`：管理欢迎页、登录页、注册页、大厅、画布之间的切换。
3. `DBClient/httpmgr.cpp`：处理 HTTP 请求和 OSS 上传。
4. `DBClient/tcpmgr.cpp`：处理 TCP 连接、拆包、发包、消息分发、重定向和断线重连。
5. `DBClient/lobbywidget.cpp`：创建房间、加入房间、头像上传、返回房间。
6. `DBClient/canvas.cpp`：管理画布 UI、工具栏、成员列表、聊天、权限、绘画网络同步。
7. `DBClient/paintscene.cpp`：处理本地绘制、远端绘制应用、离线撤销。
8. `DBClient/global.h`：前端请求 ID、错误码、房间结构、图元枚举。

### GateServer

HTTP 网关服务，基于 Boost.Beast 接收客户端 HTTP 请求，再通过 gRPC 调用 VerifyServer 或 LogicServer。

关键文件：

1. `GateServer/src/GateServer.cpp`：服务入口。
2. `GateServer/src/CServer.cpp`：接收 HTTP 连接。
3. `GateServer/src/HttpConnection.cpp`：解析 HTTP 请求并写回响应。
4. `GateServer/src/LogicSystem.cpp`：注册 HTTP 路由，例如 `/get_verifycode`、`/user_register`、`/reset_password`、`/user_login`、`/get_oss_token`、`/save_avator`。
5. `GateServer/src/VerifyGrpcClient.cpp`：调用验证码服务。
6. `GateServer/src/LogicGrpcClient.cpp`：调用账号与用户服务。

### LogicServer

gRPC 业务服务，负责账号、Token、用户资料和 CanvasServer 路由。

关键文件：

1. `LogicServer/src/LogicServer.cpp`：服务入口。
2. `LogicServer/src/LogicServiceImpl.cpp`：实现注册、重置密码、登录、更新头像。
3. `LogicServer/src/MysqlDao.cpp`：MySQL 用户表访问。
4. `LogicServer/src/MysqlPool.cpp`：MySQL 连接池。
5. `LogicServer/src/RedisMgr.cpp`：验证码和 Token 读写。

### VerifyServer

Node.js gRPC 服务，负责验证码生成、Redis 写入和邮件发送。

关键文件：

1. `VerifyServer/server.js`：gRPC 服务入口。
2. `VerifyServer/redis.js`：Redis 访问。
3. `VerifyServer/email.js`：邮件发送。
4. `VerifyServer/message.proto`：验证码服务协议。

### CanvasServer

画板 TCP 长连接服务，负责房间、会话、绘画广播、聊天广播、权限控制和历史回放。`CanvasServer2` 是同构实例，用于多实例重定向测试。

关键文件：

1. `CanvasServer/src/CanvasServer.cpp`：服务入口。
2. `CanvasServer/src/CServer.cpp`：接收 TCP 连接。
3. `CanvasServer/src/CSession.cpp`：处理 TCP 拆包、绘画快通道、会话关闭、发送队列。
4. `CanvasServer/src/LogicSystem.cpp`：处理 CanvasLogin、CreateRoom、JoinRoom、Chat、GrantEdit、RevokeEdit。
5. `CanvasServer/src/Room.cpp`：房间成员管理、广播、历史回放、编辑权限。
6. `CanvasServer/src/RoomMgr.cpp`：房间管理。
7. `CanvasServer/src/RedisMgr.cpp`：房间元信息读写。

## 协议说明

### TCP 包格式

客户端和 CanvasServer 的 TCP 包格式为：

```text
2 bytes message_id + 2 bytes message_len + message_body
```

包头使用网络字节序。客户端对应实现位于 `DBClient/tcpmgr.cpp`，服务端对应实现位于 `CanvasServer/src/CSession.cpp`。

### 实际业务消息 ID

实际 TCP 消息 ID 以 `DBClient/global.h` 和各服务 `include/*/const.h` 中的 `ReqId`、`MSG_IDS` 为准。

常用消息：

1. `ID_CANVAS_LOGIN_REQ` / `ID_CANVAS_LOGIN_RSP`：客户端登录 CanvasServer。
2. `ID_CREAT_ROOM_REQ` / `ID_CREAT_ROOM_RSP`：创建房间。
3. `ID_JOIN_ROOM_REQ` / `ID_JOIN_ROOM_RSP`：加入房间。
4. `ID_USER_JOIN_BROADCAST`：成员加入广播。
5. `ID_USER_LEAVE_BROADCAST`：成员离开广播。
6. `ID_DRAW_REQ` / `ID_DRAW_RSP`：绘画请求与绘画广播。
7. `ID_CHAT_REQ` / `ID_CHAT_RSP`：群聊请求与广播。
8. `ID_GRANT_EDIT_REQ` / `ID_GRANT_EDIT_RSP`：授权编辑。
9. `ID_REVOKE_EDIT_REQ` / `ID_REVOKE_EDIT_RSP`：取消编辑权限。
10. `ID_PERMISSION_CHANGED_BROADCAST`：编辑权限变更广播。

### DrawReq

`DrawReq` 是绘画同步的核心结构，定义在各模块的 `message.proto` 中。各服务目录下的 `message.proto` 应保持一致。

关键字段：

1. `uid`：发起绘画的用户 ID。
2. `item_id`：客户端生成的图元 UUID，用于标识同一图元的 START、MOVE、END。
3. `cmd`：绘画阶段，例如 `CMD_START`、`CMD_MOVE`、`CMD_END`。
4. `shape`：图元类型，例如 `SHAPE_PEN`、`SHAPE_RECT`、`SHAPE_OVAL`、`SHAPE_LINE`、`SHAPE_ERASER`。
5. `color`：颜色值，格式为 `0xAARRGGBB`。
6. `width`：线宽。
7. `start_x`、`start_y`：几何图形起点。
8. `current_x`、`current_y`：当前点或几何图形终点。
9. `path_points`：画笔和橡皮擦在 MOVE 阶段批量传输的增量点。

## 关键流程

### 登录流程

1. 客户端通过 GateServer 获取验证码、注册或登录。
2. GateServer 通过 gRPC 调用 VerifyServer 或 LogicServer。
3. LogicServer 校验 MySQL 用户数据，生成 Token 并写入 Redis。
4. LogicServer 返回用户信息、Token 和一个 CanvasServer 地址。
5. 客户端连接 CanvasServer，并发送 `ID_CANVAS_LOGIN_REQ` 进行 Token 鉴权。
6. 鉴权成功后进入大厅。

### 创建房间流程

1. 客户端在大厅选择房间名和画布尺寸。
2. 客户端向 CanvasServer 发送 `ID_CREAT_ROOM_REQ`。
3. CanvasServer 生成房间号，创建内存 Room。
4. CanvasServer 将房间元信息写入 Redis。
5. CanvasServer 将房主加入房间并返回房间信息。
6. 客户端进入画布窗口，房主获得编辑权限。

### 加入房间流程

1. 客户端向当前 CanvasServer 发送 `ID_JOIN_ROOM_REQ`。
2. CanvasServer 从 Redis 读取房间元信息。
3. 如果房间属于其他 CanvasServer，返回 `NeedRedirect` 和目标地址。
4. 客户端自动切换 TCP 连接，重新 CanvasLogin，并再次发送 JoinRoom。
5. 目标 CanvasServer 将用户加入房间，返回成员快照。
6. 如果房间内存中已有历史操作，新用户会收到历史回放。

### 绘画同步流程

1. `PaintScene` 采集本地鼠标输入。
2. `Canvas` 将绘画事件组装成 `DrawReq`。
3. 客户端通过 `TcpMgr` 发送 `ID_DRAW_REQ`。
4. `CSession` 快通道校验请求并广播 `ID_DRAW_RSP`。
5. 远端客户端收到广播后调用 `PaintScene::applyRemoteDraw()` 渲染图元。
6. 服务端将可回放的绘画操作追加到 Room 内存历史。

### 断线重连流程

1. 客户端监听 `QTcpSocket::disconnected`。
2. 断线后回到 Lobby，并启动指数退避重连。
3. 重连成功后自动发送 CanvasLogin。
4. CanvasLogin 成功后自动 Join 上次房间。
5. Join 成功后恢复 Canvas 界面，并通过历史回放恢复画布状态。

## 构建与运行

各模块独立构建，根目录没有统一的顶层 CMake。

客户端：

1. 进入 `DBClient`。
2. 使用 Qt 6.5.3 和 vcpkg 提供的 Protobuf、gRPC 依赖构建。
3. 准备 `config.ini`，配置 GateServer 地址。

C++ 服务：

1. `GateServer`、`LogicServer`、`CanvasServer`、`CanvasServer2` 均有各自的 `CMakeLists.txt`。
2. 各服务运行目录需要准备对应的 `config.ini`，可参考 `config.ini.example`。
3. 需要可用的 MySQL、Redis、gRPC、Protobuf、JsonCpp、Boost 等依赖。

VerifyServer：

1. 进入 `VerifyServer`。
2. 执行 `npm install` 安装依赖。
3. 准备 `config.json`，配置 Redis、邮箱等信息。
4. 执行 `node server.js` 启动验证码服务。

推荐启动顺序：

1. 启动 MySQL 和 Redis。
2. 启动 VerifyServer。
3. 启动 LogicServer。
4. 启动 GateServer。
5. 启动 CanvasServer 和 CanvasServer2。
6. 启动 DBClient。

## 已知限制

1. 绘画历史当前只保存在 CanvasServer 内存中，服务进程重启后无法恢复完整笔迹。
2. 清屏、联机撤销相关协议字段已有预留，但完整的服务端校验与广播链路尚未实现。
3. 多人同时 Join 与绘画时，历史回放和实时操作之间还没有全局序列号，极端情况下可能出现弱一致。
4. 各服务目录存在多份 `message.proto` 和生成文件，修改协议时需要同步更新并重新生成。
5. 部分注释文件存在编码不一致现象，建议后续统一为 UTF-8。
