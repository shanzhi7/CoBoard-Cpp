#pragma once
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <memory>
#include <iostream>
#include "CanvasServer/message.pb.h"

// 前置声明，避免循环引用
class CSession;

class Room : public std::enable_shared_from_this<Room>
{
public:
	Room(const std::string& roomId);
	~Room();

	std::string GetRoomId() const;								//获取房间ID

	void SetRoomInfo(const std::string& name, int owner_uid);	//设置房间信息
	int GetOwnerUid() const;									//获取房主ID

	void Join(std::shared_ptr<CSession> session);				//加入房间
	void Leave(int uid);										//用户离开

	// 广播消息
	void Broadcast(const std::string& data, int msg_id, int exclude_uid = 0);	//exclude_uid,排除自己

	//广播辅助函数
	void BroadcastUserEnter(std::shared_ptr<CSession> session);		//广播用户进入
	void BroadcastUserLeave(int uid);								//广播用户离开

	//获取房间成员快照
	std::vector<message::UserInfo> GetMemberSnapshot();

private:
	std::string _room_id;
	std::string _name;
    int _owner_uid = 0;

	std::mutex _mutex;		//// 互斥锁：保护 _sessions 和 _history
	std::map<int, std::shared_ptr<CSession>> _sessions;	// 房间内的用户列表: UID -> Session

	// 【画板关键】历史笔迹
	// 暂存所有画画的指令，新用户进来时要把这些发给他，否则他看到的是白板
	std::vector<std::string> _history;
};