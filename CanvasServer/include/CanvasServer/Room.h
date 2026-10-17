/***********************************************************************************
* @file         Room.h
* @brief        房间成员、编辑权限和图元历史状态
* @author       shanzhi
* @date         2026/10/08
* @history
***********************************************************************************/
#pragma once
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>
#include <deque>
#include <iostream>
#include "CanvasServer/message.pb.h"
#include "CanvasServer/const.h"

// 前置声明，避免循环引用
class CSession;

class Room : public std::enable_shared_from_this<Room>
{
public:
	struct HistoryEntry
	{
		short _msg_id;                 // 历史消息的 TCP 消息 ID，回放时必须按原类型发送
		std::string _body;             // 历史消息的 protobuf 二进制正文，不包含四字节包头
		std::string _image_item_id;    // 图片创建条目关联的 item_id；非图片或图片删除条目为空，用于历史裁剪后重建创建条目索引
		std::string _text_item_id; // 文字创建条目 ID，用于原位更新和裁剪后重建索引。
		std::string _draw_item_id; // 绘画完整操作组标识，历史裁剪不能拆散笔迹。
		std::string _edit_body; // 创建或结束消息之后回放的最新通用编辑状态。
	};

	Room(const std::string& roomId); // 创建指定 ID 的房间。
	~Room(); // 释放房间成员引用和图元历史。

	std::string GetRoomId() const;								//获取房间ID

	void SetRoomInfo(const std::string& name, int owner_uid);	//设置房间信息
	int GetOwnerUid() const;									//获取房主ID
	bool IsOwner(int uid) const;								//判断是否为房主
	bool HasMember(int uid) const;							//判断用户是否在当前房间
	bool CanEdit(int uid) const;							//判断用户是否有画板编辑权限
	bool GrantEdit(int uid);								//授权用户编辑画板
	bool RevokeEdit(int uid);								//取消用户编辑权限

	bool TryJoin(std::shared_ptr<CSession> session); // 仅接受活动房间的有效会话。
	void Join(std::shared_ptr<CSession> session);				//加入房间
	bool Leave(int uid, const CSession* expected_session); // 移除预期会话及编辑授权，不操作 Redis。
	void Leave(int uid);										//用户离开

	// 广播消息
	void Broadcast(const std::string& data, int msg_id, int exclude_uid = 0);	//exclude_uid,排除自己

	//广播辅助函数
	void BroadcastUserEnter(std::shared_ptr<CSession> session);		//广播用户进入
	void BroadcastUserLeave(int uid);								//广播用户离开

	//获取房间成员信息快照
	std::vector<message::UserInfo> GetMemberSnapshot(); // 复制当前成员资料，供锁外发送。
	
	//获取房间成员session快照
	std::vector<std::shared_ptr<CSession>> GetMemberSessionSnapshot(int exclude_uid = 0); // 复制未被排除的成员会话。

	// --画板历史(存内存)--
	void AppendHistory(const std::string& raw_body, short msg_id = ID_DRAW_RSP); // 保存一条带消息类型的可回放操作
	std::vector<HistoryEntry> GetHistorySnapshot();		// 线程安全拷贝一份用于回放
	void ClearHistory();								// 清除绘画、图片和文字历史及状态。

	bool ApplyImageOperation(message::ImageOperation& operation, std::string* normalized_body = nullptr); // 在房间锁内更新图片图元、序号并原子生成广播正文
	bool ApplyTextOperation(const message::TextOperation& operation, message::TextOperationRsp& response); // 原子更新文字状态、版本和历史。
	bool ApplyItemEditOperation(const message::ItemEditOperation& operation, message::ItemEditOperationRsp& response, const CSession* expected_session = nullptr); // 原子校验版本并更新单个图元。
	bool AcceptItemEdit(std::shared_ptr<CSession> session, const message::ItemEditOperation& operation); // 快通道原子处理预览或最终提交并按版本顺序入发送队列。
	void FillItemEditState(message::ItemEditOperationRsp& response); // 填写目标当前完整状态和版本。
	void FillTextState(message::TextOperationRsp& response); // 失败响应附带当前目标状态和版本。

	bool BeginLeave(int uid, const CSession* expected_session); // 标记成员退出中，暂停其房间操作。
	bool ContainsSession(int uid, const CSession* expected_session) const; // 比较具体成员会话，保护重连。
	bool BeginClose(); // 封住房间，重复解散保持关闭中。
	bool IsClosing() const; // 返回房间是否禁止新操作。
	std::vector<std::shared_ptr<CSession>> FinishClose(); // 清空全部状态并返回结束通知快照。
	bool AcceptDraw(std::shared_ptr<CSession> session, const message::DrawReq& request, const std::string& body); // 原子接受绘画、记录历史并入队广播。

private:
	bool ApplyItemEditOperationLocked(const message::ItemEditOperation& operation, message::ItemEditOperationRsp& response, const CSession* expected_session); // 持有房间锁时校验、生成预览或提交最终状态。
	void FillItemEditStateLocked(message::ItemEditOperationRsp& response); // 持锁填写规范状态，删除后仍返回最后版本。
	void ClearItemEditStateLocked(); // 清空编辑索引、笔迹状态和请求幂等缓存。
	void RebuildDrawHistoryIndexLocked(); // 历史裁剪后重建已完成笔迹的结束下标。
	void UpdateItemEditHistoryLocked(const std::string& item_id); // 将最新编辑状态附着于既有创建记录。
	void SyncLegacyTextLocked(const message::TextOperation& operation, const message::TextItem& state, std::uint64_t revision); // 旧文字入口维护共享版本和扩展布局。
	void SyncLegacyImageLocked(const message::ImageOperation& operation); // 旧图片入口维护共享版本和资源所属状态。
	bool RegisterDrawLocked(const message::DrawReq& request); // 关联完整笔迹并在结束后登记可编辑状态。
	bool IsActiveMemberLocked(int uid) const; // 已持有房间锁时检查活动成员。
	void RebuildTextHistoryIndexLocked(); // 裁剪后重新建立文字创建历史下标。
	void FillTextStateLocked(message::TextOperationRsp& response); // 已持有房间锁时填写目标当前状态。
	void AppendHistoryLocked(HistoryEntry entry); // 在已持有房间锁时追加历史并执行裁剪；公共入口 AppendHistory 负责加锁
	void RebuildImageHistoryIndexLocked(); // 历史裁剪导致下标偏移后重建图片创建条目索引；必须在房间锁内调用

	std::string _room_id; // 房间唯一标识。
	std::string _name; // 房间显示名称。
    int _owner_uid = 0; // 房主用户 ID。

	bool _is_closing = false; // 关闭开始后不再接受任何房间业务。
	bool _is_closed = false; // Redis 与内存清理完成的终态。
	std::unordered_set<int> _leaving_users; // Redis 清理未确认的退出中成员。
	mutable std::mutex _mutex;		// 互斥锁：保护 _sessions、_history 和房间状态
	std::map<int, std::shared_ptr<CSession>> _sessions;	// 房间内的用户列表: UID -> Session
	std::unordered_set<int> _editable_users;			//被房主授权可编辑画板的用户集合

	// 历史记录同时保存消息 ID 和 protobuf body，避免图片操作回放时被误发为绘画消息。
	std::vector<HistoryEntry> _history; // 按绘制顺序保存带消息类型的图元历史。
	std::unordered_map<std::string, message::ImageItem> _image_items; // 当前房间的图片图元索引，键为 item_id
	std::unordered_map<std::string, std::size_t> _image_create_index; // 图片 item_id 到创建历史条目下标的映射；变换更新原位改写创建条目而不是追加移动记录，保证新成员回放时图片直接出现在最新位置
	std::uint64_t _image_sequence = 0; // 服务端为图片操作分配的单调序号，进程内有效
	std::unordered_map<std::string, message::TextItem> _text_itemMap; // 当前文字完整状态。
	std::unordered_map<std::string, std::size_t> _text_create_indexMap; // 文字创建条目所在的历史下标。
	std::unordered_map<std::string, std::uint64_t> _text_sequenceMap; // 每个目标最新版本，删除后也保留版本。
	std::uint64_t _text_sequence = 0; // 文字操作的房间单调顺序号。

	std::unordered_map<std::string, message::ItemEditState> _item_edit_states; // 图元公共变换和扩展布局，旧类型字段同步更新。
	std::unordered_map<std::string, std::uint64_t> _item_revisions; // 所有编辑入口共享的目标版本，删除保留墓碑。
	std::unordered_map<std::string, message::DrawReq> _draw_started; // 尚未完成的笔迹起点和创建者。
	std::unordered_map<std::string, std::size_t> _draw_end_index; // 完整笔迹结束消息在历史中的下标。
	std::unordered_map<std::string, message::ItemEditOperationRsp> _edit_responses; // 最近成功请求的幂等确认缓存。
	std::deque<std::string> _edit_response_order; // 幂等缓存淘汰顺序，限制长期房间内存。
	std::uint64_t _edit_sequence = 0; // 跨图片、文字和通用编辑的服务端单调版本。

	// 防止房间画太久内存爆
	static constexpr size_t MAX_HISTORY_OPS = 10000;	// 最大保存的笔迹数
	static constexpr size_t MAX_IMAGE_ITEMS = 100;	// 单个房间允许保留的图片图元数量上限
};
