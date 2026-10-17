/***********************************************************************************
* @file         const.h
* @brief        TCP 消息编号和房间公共数据
* @author       shanzhi
* @date         2026/10/08
* @history
***********************************************************************************/
#pragma once
#include <functional>
#include <string>

//enum ErrorCodes
//{
//	Success = 0,
//	Error_Json = 1001,		//Json解析错误
//	RPCFailed = 1002,		//RPC请求错误
//	VarifyExpired = 1003,	//验证码过期
//	VarifyCodeErr = 1004,	//验证码错误
//	UserExist = 1005,		//用户已经存在
//	PasswdErr = 1006,		//密码错误
//	EmailNotMatch = 1007,	//邮箱不匹配
//	PasswdUpFailed = 1008,	//更新密码失败
//	PasswdInvalid = 1009,	//密码更新失败
//	TokenInvalid = 1010,	//token无效
//	UidInvalid = 1011,		//uid无效
//};

enum MSG_IDS {
	ID_LEAVE_ROOM_REQ = 1033, // 普通成员退出房间请求。
	ID_LEAVE_ROOM_RSP = 1034, // 普通成员退出完成。
	ID_DESTROY_ROOM_REQ = 1035, // 房主解散房间请求。
	ID_DESTROY_ROOM_RSP = 1036, // 房主解散完成。
	ID_ROOM_DESTROYED_BROADCAST = 1037, // 房间解散通知。
	ID_GET_VERIFY_CODE = 1001,                  //获取验证码
	ID_REGISTER = 1002,                         //注册账号
	ID_RESET_PWD = 1003,                        //重置密码
	ID_LOGIN_REQ = 1004,                        //登录
	ID_JOIN_ROOM_REQ_DEL = 1005, // 废弃的加入房间编号，保留兼容。
	ID_DRAW_REQ_DEL = 1006, // 废弃的绘画编号，保留兼容。
	ID_CANVAS_LOGIN_REQ = 1007,                 //登录到CanvasServer
	ID_CANVAS_LOGIN_RSP = 1008,                 //登录CanvasServer回包
	ID_CREAT_ROOM_REQ = 1009,                   //创建房间请求
	ID_CREAT_ROOM_RSP = 1010,                   //创建房间回包
	ID_JOIN_ROOM_REQ = 1011,                    //加入房间请求
	ID_JOIN_ROOM_RSP = 1012,                    //加入房间回包
	ID_GET_OSS_TOKEN = 1013,                    //获取OSS上传签名
	ID_UPLOAD_IMAGE = 1014,                     //上传图片到OSS
	ID_SAVE_IMAGE = 1015,                       //将图片保存到数据库
	ID_USER_JOIN_BROADCAST = 1016,				//用户加入房间广播
	ID_USER_LEAVE_BROADCAST = 1017,				//用户离开房间广播
	ID_DRAW_REQ = 1018,      // 绘画请求（客户端 -> CanvasServer）
	ID_DRAW_RSP = 1019,      // 绘画广播（CanvasServer -> 房间其他人）
	ID_CHAT_REQ = 1020,							//群聊天请求
    ID_CHAT_RSP = 1021,							//群聊天回包
    ID_GRANT_EDIT_REQ = 1022,    //授权编辑请求
    ID_GRANT_EDIT_RSP = 1023,    //授权编辑回包
    ID_REVOKE_EDIT_REQ = 1024,    //取消编辑权限请求
    ID_REVOKE_EDIT_RSP = 1025,    //取消编辑权限回包
    ID_PERMISSION_CHANGED_BROADCAST = 1026,    //编辑权限变更广播
    ID_ITEM_EDIT_REQ = 1040, // 通用图元编辑请求。
    ID_ITEM_EDIT_RSP = 1041, // 通用图元编辑确认及广播。
    ID_TEXT_OPERATION_REQ = 1031, // 文字操作请求。
    ID_TEXT_OPERATION_RSP = 1032, // 文字操作确认和广播。
    ID_IMAGE_OPERATION_REQ = 1027,              //图片图元操作请求，正文为 ImageOperation
    ID_IMAGE_OPERATION_RSP = 1028,              //图片图元操作广播，正文为服务端规范化后的 ImageOperation
};
#define MAX_LENGTH 1024*64

class Defer
{
public:
	Defer(std::function<void()> func) :_func(std::move(func)) // 保存作用域结束时执行的清理回调。
	{
		// 1. 回调归当前守卫所有，离开作用域时统一执行。
	}

	//作用域结束自动调用
	~Defer() // 离开作用域时执行一次清理。
	{
		// 1. 有回调时执行清理，空回调无需处理。
		if (_func)
			_func();	//执行清理逻辑
	}

	// 禁止拷贝（避免重复执行）
	Defer(const Defer&) = delete; // 禁止复制以避免重复清理。
	Defer& operator=(const Defer&) = delete; // 禁止赋值以避免重复清理。
private:
	std::function<void()> _func;	//存储延迟执行的函数
};

// 定义房间信息结构体
struct RoomInfo {
	std::string id; // 房间唯一标识。
	std::string name; // 房间名称。
	int owner_uid; // 房主用户 ID。
	std::string host; // CanvasServer IP
	int port;         // CanvasServer Port
	int width = 1920; // 画布宽度，单位为场景像素。
	int height = 1080; // 画布高度，单位为场景像素。
};

#define CODEPREFIX "code_"
#define TOKEN_PREFIX "utoken_"
#define UID_PREFIX "uid_token_" //用于通过 uid 找 token
#define ROOM_PREFIX "canvas:room:"
#define ROOM_USERS_PREFIX "room_users:"
