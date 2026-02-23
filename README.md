# SyncCanvas

多人协作画板项目（个人简历项目，非公司项目）。前端使用 Qt 6.5.3 + Protobuf（Google），后端使用 gRPC、Asio、JsonCpp、MySQL、Redis 等。

**当前进度（2026-02-23）**
1. 前端：本地绘画系统完善（画笔、矩形、椭圆、直线、橡皮擦），支持实时预览与远端同步渲染。
2. 后端：GateServer、LogicServer、VerifyServer、CanvasServer（多实例 + 重定向）已稳定联调运行。
3. 房间系统：
   - 创建/加入房间
   - NeedRedirect 自动切服（跨 CanvasServer 路由）
   - 成员加入/离开广播
   - 在线用户列表实时同步（UI 已修复重复/残留问题）
   - 房间历史回放（已实现，内存级）：
     - CanvasServer 维护 Room::_history（绘画操作序列）
     - 新用户加入房间自动回放历史笔迹
     - 断线重连后重新 Join 可恢复画布状态（避免白板）

4. 绘画同步（已稳定）：
   - START / MOVE / END 三阶段同步
   - Pen/Eraser 支持增量点同步（path_points）
   - 几何图形（Rect/Oval/Line）实时 MOVE 预览
   - 服务端快通道转发（ID_DRAW_REQ → ID_DRAW_RSP）
   - 高频绘画数据不进入 LogicQueue，降低延迟与锁竞争

5. 带宽优化：
   - 画笔/橡皮擦 16ms 定时器节流 + 批量发送
   - 单包最大点数限制（防止大包突刺）

6. 断线重连与会话恢复（已实现）：
   - 监听 QTcpSocket::disconnected 自动进入重连流程
   - 指数退避重连（200ms → 500ms → 1s → 2s → 5s 上限）
   - 自动 CanvasLogin（Token 鉴权）
   - 自动重新 Join 上次房间（支持 NeedRedirect 二次切服）
   - 重连成功后自动恢复 Canvas 界面
   - 配合 Room::_history 回放，实现画布状态恢复（弱一致）

7. 服务端重启恢复（关键工程优化）：
   - JoinRoom 支持 Lazy Load 房间
   - 当 RoomMgr 内存不存在房间时，从 Redis 读取 room_info 自动恢复
   - 解决「服务器重启后无法回到房间」问题

8. UI 稳定性修复：
   - 断线后自动返回 Lobby 并提示“正在重连”
   - Canvas resetScene 清理残留图元与用户列表
   - 修复重复用户显示与场景残留 Bug

**后续开发计划（Roadmap）**

- **优先级 1：协作核心体验（高价值）**
   1. 历史回放（History）：
   - 已完成：CanvasServer 内存级 Room::_history
   - 已支持：新用户入房自动回放历史绘画数据
   - 已支持：断线重连后重新 Join 恢复画布状态
   - 待优化：历史操作序列 Seq 化（解决 Join 期间的绘画一致性问题）
   - 后续扩展：Redis 持久化（服务重启可恢复完整绘画记录）

  2. CMD_CLEAR（清屏）：
     - 定义协议并广播清屏指令
     - 客户端同步 resetScene()

  3. CMD_UNDO（撤销）：
     - 按 item_id 回滚图元
     - 支持多人协作撤销策略

- **优先级 2：功能扩展（产品化）**
  1. 直线工具 UI 按钮（逻辑已完成，待接入工具栏）
  2. 群聊/房间聊天系统（TCP 同步文本消息）
  3. 画笔参数扩展（虚线、透明度、图层）

- **优先级 3：工程化与简历加分项**
  1. Redis 历史持久化（绘画记录分片/压缩）
  2. 性能监控：
     - Draw 包 QPS
     - 房间人数上限
     - 带宽统计
  3. 智能绘图（OpenCV 规划）：
     - 手绘图形识别（圆/矩形拟合）
     - 自动矢量化标准图形

## 稳定性设计（项目亮点）

1. 控制面 + 数据面分离：
   - Gate/Logic 负责鉴权与路由（控制面）
   - CanvasServer 专注实时绘画同步（数据面）

2. 自动重连机制：
   - TCP 断线检测 + 指数退避重连
   - 会话自动恢复（Login + Join）
   - 支持服务器重启场景恢复

3. 房间懒加载（Lazy Restore）：
   - 房间信息持久化到 Redis
   - 服务重启后按需恢复 Room 到内存
   - 避免房间与服务器强绑定导致的恢复失败

4. 网络优化策略：
   - 增量点同步（path_points）
   - 16ms 批量 flush 降低带宽占用
   - 单包点数上限防止拥塞突刺

5. 实时协作状态同步设计（核心难点）：
   - 服务端维护房间级操作历史（Room::_history）
   - 新用户加入房间时先回放历史操作，再接收实时广播
   - 绘画数据采用操作流（DrawReq）而非最终位图同步，降低带宽占用
   - 快通道（Draw）与慢通道（Join/Login）分离，避免高频数据阻塞业务线程

6. Asio 并发安全优化（工程级修复）：
   - 修复多线程下同一 socket 并发 async_write 导致的崩溃问题
   - 发送队列 + executor 串行化写链（post 到 socket executor）
   - 避免 IOCP 未定义行为（Undefined Behavior）
   - 提升高并发绘画广播稳定性


**关键结构与协议说明（用于后续协作开发）**
1. `message.proto`（后端/前端共享）
2. `const.h`（后端）
3. `global.h`（前端）
4. `TcpMgr`（前端网络层）
5. `Canvas`（前端画板窗口）
6. `PaintScene`（前端绘图逻辑）

**1) message.proto（画板同步相关结构）**
相关文件：`CanvasServer/message.proto`（其他服务目录中同名文件保持一致）。

**消息类型（MsgID）**
1. `ID_DRAW_REQ` / `ID_DRAW_RSP`：绘画同步请求 / 广播
2. `ID_CLEAR_REQ` / `ID_CLEAR_RSP`：清屏同步请求 / 广播

**绘画指令与图元类型**
1. `DrawCmd`：`CMD_START`（按下开始）、`CMD_MOVE`（拖拽预览）、`CMD_END`（松开落笔）、`CMD_UNDO`、`CMD_CLEAR`
2. `ShapeType`：`SHAPE_PEN`、`SHAPE_RECT`、`SHAPE_OVAL`、`SHAPE_LINE`、`SHAPE_ERASER`

**DrawReq（核心同步结构）**
1. `uid`：发起者用户 ID
2. `item_id`：图元 UUID（客户端生成，标识同一图元的连续更新）
3. `cmd`：绘画指令（START/MOVE/END 等）
4. `shape`：图元类型（Pen/Rect/Oval/Line/Eraser）
5. `color`：颜色（0xAARRGGBB）
6. `width`：线宽
7. `pen_style`：线型（可选）
8. `start_x/start_y`、`current_x/current_y`：几何图形用起点与当前点
9. `path_points`：自由笔刷/橡皮擦的点序列（MOVE 阶段仅发增量点）

**错误码（ErrorCodes）**
1. 基础/系统错误：`Error_Json`、`RPCFailed`、`RedisErr`、`MysqlErr`、`ServerInternalErr` 等
2. 用户相关：`UserExist`、`UserNotExist`、`PasswdErr`、`TokenInvalid`、`UidInvalid` 等
3. 房间相关：`RoomCreateFailed`、`RoomNotExist`、`RoomFull`、`NotInRoom`、`NotOwner` 等
4. 重定向：`NeedRedirect`（加入房间后需要切到指定 CanvasServer）

**2) 后端 const.h（跨服务常量与工具）**
相关文件：
1. `GateServer/include/GateServer/const.h`
2. `LogicServer/include/LogicServer/const.h`
3. `CanvasServer/include/CanvasServer/const.h`

说明：
1. `Defer`：RAII 风格的延迟清理工具（作用域结束自动执行）。
2. `CODEPREFIX`、`TOKEN_PREFIX`、`UID_PREFIX` 等：Redis key 前缀。
3. `ROOM_PREFIX`、`ROOM_USERS_PREFIX`：房间信息与成员集合的 key 前缀（CanvasServer）。
4. `MSG_IDS`：后端 TCP 消息 ID 定义（与前端 `ReqId` 对齐）。

**3) 前端 global.h（全局数据结构与枚举）**
相关文件：`DBClient/global.h`

说明：
1. `ReqId`：前端 TCP 消息 ID（与后端 `MSG_IDS` 对齐）。
2. `ErrorCodes`：前端基础错误（JSON 解析、网络错误）。
3. `Modules`：模块枚举（注册、重置、登录、大厅）。
4. `ServerInfo`：后端连接信息（Host/Port/Token/Uid）。
5. `RoomInfo`：房间信息（房间号、房主、CanvasServer 地址、画布尺寸、成员列表）。
6. `ShapeType`：前端绘图类型（顺序与 Proto `ShapeType` 保持一致）。
7. `repolish` / `applyColor`：QSS 刷新与图标染色工具函数。

**4) 前端 TcpMgr（TCP 管理与消息分发）**
相关文件：`DBClient/tcpmgr.h`、`DBClient/tcpmgr.cpp`

说明：
1. 负责 TCP 连接、发包、收包、拆包。
2. 采用 `quint16` 的 `message_id + message_len` 作为包头，`BigEndian`。
3. `initHandlers()` 注册各类消息处理函数。
4. 已支持的业务：Canvas 登录回包、创建/加入房间回包、用户加入/离开广播。
5. 支持 `NeedRedirect`：当加入房间返回重定向时，自动切换到指定 CanvasServer，并重发 JoinRoomReq。

**5) 前端 Canvas（画板窗口与房间成员列表）**
相关文件：`DBClient/canvas.h`、`DBClient/canvas.cpp`

说明：
1. 管理 UI：画板区域、工具栏、状态栏、用户列表。
2. 负责接收 `TcpMgr` 的用户加入/离开广播并更新列表。
3. 管理 `PaintScene` 与工具按钮（Pen/Eraser/Rect/Oval）。
4. `eventFilter` 处理用户列表点击行为，避免误选状态。

**6) 前端 PaintScene（本地绘制逻辑）**
相关文件：`DBClient/paintscene.h`、`DBClient/paintscene.cpp`

说明：
1. 继承 `QGraphicsScene`，处理鼠标按下/移动/抬起。
2. 支持图元：画笔、矩形、椭圆、直线、橡皮擦。
3. 维护当前图元与状态：`_currUuid`、`_currShapeType`、`_startPos`、`_currPathItem` 等。
4. 提供网络同步信号：`sigStrokeStart`、`sigStrokeMove`、`sigStrokeEnd`（已在 Canvas 中连接并发送 `DrawReq`）。

**绘画同步实现（已落地）**
1. `PaintScene` 采集本地输入：按下/移动/抬起分别触发 `sigStrokeStart/Move/End`。
2. `Canvas` 连接上述信号并组包 `message::DrawReq`（START/MOVE/END），通过 `TcpMgr` 发送 `ID_DRAW_REQ`（1018）。
3. Pen/Eraser：MOVE 阶段缓存增量点，按 16ms 定时批量 flush 到 `path_points`，降低网络开销。
4. `TcpMgr` 注册 `ID_DRAW_RSP`（1019）处理：收到广播后发出 `sig_draw_broadcast`，由 `Canvas` 调用 `PaintScene::applyRemoteDraw()` 完成远端渲染。


**架构与流程说明（文字版）**
1. 架构角色
2. 客户端（Qt）：负责 UI、绘制、输入采集、TCP 长连接通信。
3. GateServer：HTTP/REST 网关，转发注册/登录/重置密码等请求。
4. VerifyServer：验证码服务（邮箱验证码获取与校验）。
5. LogicServer：业务逻辑与数据层（账号、Token、房间路由、持久化）。
6. CanvasServer（两台）：画板 TCP 长连接服务，负责房间内同步广播、房间成员管理。

1. 典型登录流程（文字时序）
2. 客户端请求验证码 -> GateServer -> VerifyServer。
3. 客户端注册/登录 -> GateServer -> LogicServer（MySQL/Redis）。
4. LogicServer 返回 `token` + `CanvasServer` 地址。
5. 客户端直连 CanvasServer，进行长连接鉴权。

1. 进入房间流程（文字时序）
2. 客户端请求创建/加入房间 -> CanvasServer。
3. 如果需要负载均衡，返回 `NeedRedirect` + 目标 CanvasServer 地址。
4. 客户端切换目标 CanvasServer，重发 JoinRoomReq。
5. 成功后，返回房间信息 + 成员列表，并向房间内广播加入事件。

1. 绘画同步流程（已实现）
2. 本地绘制 -> 发送 `DrawReq`（START/MOVE/END）到 CanvasServer（1018）。
3. CanvasServer 校验 UID 并广播 `ID_DRAW_RSP`（1019）给房间内其他成员。
4. 远端客户端根据 `item_id` 查找/创建图元并更新渲染。
5. 下一步扩展：历史回放（Room::_history/Redis）、清屏（CMD_CLEAR）、撤销（CMD_UNDO）、断线重连恢复。
