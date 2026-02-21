# SyncCanvas

多人协作画板项目（个人简历项目，非公司项目）。前端使用 Qt 6.5.3 + Protobuf（Google），后端使用 gRPC、Asio、JsonCpp、MySQL、Redis 等。

**当前进度（2026-02-22）**
1. 前端：本地绘画完成（画笔、矩形、椭圆、直线、橡皮擦等），并已接入网络同步。
2. 后端：GateServer、LogicServer、VerifyServer、CanvasServer（支持多实例/重定向）已完成并可联调运行。
3. 房间能力：创建/加入房间、NeedRedirect 自动切服、成员加入/离开广播、在线列表 UI 同步已完成。
4. 绘画同步：客户端发送 `ID_DRAW_REQ`（1018），CanvasServer 校验 `req.uid == session.uid` 后转发广播 `ID_DRAW_RSP`（1019）；远端根据 `DrawReq.item_id` 创建/更新图元并实时渲染。
5. 带宽优化：Pen/Eraser 支持 16ms 节流批量发送 `path_points`（增量点），几何图形（Rect/Oval/Line）MOVE 实时发送保证预览。

**接下来开发计划（按优先级）**
- **优先级 1：稳定性/可用性（必须）**
  1. 断线重连与会话恢复：TCP 断开自动重连、重新鉴权、自动重新加入房间（必要时再次处理 NeedRedirect）。
  2. Join/Leave 的健壮性：异常断开触发 `Room::Leave` 广播、客户端 UI 清理与“连接状态”展示完善。
- **优先级 2：协作体验（高加分）**
  1. 新用户入房历史回放：先用 CanvasServer 内存 `Room::_history` 跑通（限制长度/按条目裁剪），后续再扩展到 Redis 持久化。
  2. CMD_CLEAR（清屏）与 CMD_UNDO（撤销）：定义并实现请求/广播，客户端根据 item_id 做删除/回滚。
- **优先级 3：工程化/扩展（简历爆点）**
  1. 历史持久化与多机一致：Redis 存储房间绘画记录（支持新用户快速加载、服务重启恢复），必要时做压缩/分片。
  2. 性能与观测：绘画包 QPS/带宽统计、房间人数上限/背压策略、日志分级（INFO/WARN/ERROR）。


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
