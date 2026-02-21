# CoBoard-Cpp - 多人实时协作画板（C++ / Qt）

> 基于 C++ 与 Qt Graphics View Framework 实现的多人实时协作画板系统，  
> 支持房间协作、实时绘画同步、头像上传与完整客户端-服务端架构设计。

<img width="1156" height="763" alt="服务端设计" src="https://github.com/user-attachments/assets/66c72f37-0523-4929-bf71-e7ffd0e7bd25" />


---

## 📌 项目简介

CoBoard 是一个基于 **Client-Server 架构** 的多人实时协作画板系统，  
主要解决“高频绘画数据实时同步”的问题，支持多用户在同一房间内进行实时协作绘图。

系统采用 **控制面 + 数据面分离设计**：
- 控制面（HTTP）：登录 / 注册 / 创建房间 / 业务逻辑
- 数据面（TCP 长连接）：实时绘画数据同步（低延迟）

核心目标：
- 低延迟实时同步绘画
- 高并发房间广播
- 可扩展的协作系统架构

---

## 🧱 技术栈

| 模块 | 技术 |
|------|------|
| 客户端 | Qt6、QGraphicsView、C++11 |
| 网络通信 | TCP、Boost.Asio、自定义二进制协议 |
| 序列化 | Protobuf |
| 后端架构 | GateServer / LogicServer / CanvasServer |
| 数据存储 | Redis、MySQL |
| 工程构建 | CMake |
| 第三方服务 | 阿里云 OSS（头像上传） |

---

## 🏗️ 系统架构设计

### 架构特点
- 控制面使用 HTTP 短连接处理业务逻辑
- 数据面使用 TCP 长连接处理高频绘画同步
- 支持房间重定向到房主所在 CanvasServer 实例
- 降低实时数据对业务服务器的压力

---

## 🎨 核心功能

### 👤 用户系统
- 验证码注册 / 登录
- 重置密码流程
- Token 鉴权机制
- 用户头像上传（阿里云 OSS）
- Lobby 大厅用户列表展示

### 🏠 房间协作系统
- 创建房间 / 加入房间
- 房间成员实时同步
- 用户加入/离开广播
- 房主服务器重定向机制

### ✏️ 实时协作绘画（核心）
- 支持多图元绘制：
  - Pen（自由画笔）
  - Rect（矩形）
  - Oval（椭圆）
  - Line（直线）
  - Eraser（橡皮擦）
- 多人同屏实时绘画同步
- 远端图元增量渲染（QGraphicsScene）

---

## 🚀 实时同步技术设计（项目亮点）

### 1️⃣ 自定义二进制通信协议
优势：
- 解决 TCP 粘包 / 拆包问题
- 降低协议解析开销
- 提高传输效率

---

### 2️⃣ Protobuf 绘画协议（DrawReq）
核心字段：
- uid：用户ID
- item_id：图元UUID
- cmd：START / MOVE / END
- shape：图元类型
- path_points：增量点数据（高频优化）

---

### 3️⃣ 高频绘画节流优化（关键优化）
问题：
> 自由画笔在鼠标移动时会产生极高频数据包（上百次/秒）

解决方案：
- 采用 **16ms 定时节流（≈60FPS）**
- 批量缓存 path_points
- 合并为单个 DrawReq 发送

效果：
- 显著降低网络带宽开销
- 提升多用户同步流畅度
- 减少服务器广播压力

---

### 4️⃣ 房间广播架构（Room / Session）
- 每个 Room 维护 Session 集合
- 收到绘画请求后：
  - 校验 uid 与 Session 绑定关系
  - 广播给房间内其他用户（Exclude Sender）
- 避免回显自身绘制数据

---

## 🖥️ 客户端设计（Qt）

### Graphics View 高性能绘图
- 基于 QGraphicsScene + QGraphicsItem
- 支持矢量图元动态更新
- 远端绘画增量渲染（applyRemoteDraw）

### UI 架构
- NetworkManager 统一管理网络请求
- Scene 与网络逻辑解耦（信号槽）
- 自定义工具栏（颜色/粗细/图形）

---

## 🔒 安全与稳定性设计
- Token 鉴权连接 CanvasServer
- uid 与 Session 绑定校验（防伪造数据）
- Protobuf 解析异常检测与调试日志
- 二进制协议日志辅助定位网络问题

---

## 🧪 项目难点与调试经验
- 调试 Protobuf 在自定义 TCP 协议下的解析问题
- 通过二进制日志（head/tail）定位序列化错误
- 解决高频绘画导致的网络性能瓶颈
- 优化多用户实时同步一致性

---

