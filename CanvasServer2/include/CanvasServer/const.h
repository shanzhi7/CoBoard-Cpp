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
	ID_GET_VERIFY_CODE = 1001,                  //获取验证码
	ID_REGISTER = 1002,                         //注册账号
	ID_RESET_PWD = 1003,                        //重置密码
	ID_LOGIN_REQ = 1004,                        //登录
	ID_JOIN_ROOM_REQ_DEL = 1005,
	ID_DRAW_REQ_DEL = 1006,
	ID_CANVAS_LOGIN_REQ = 1007,                 //登录到CanvasServer
	ID_CANVAS_LOGIN_RSP = 1008,                 //登录CanvasServer回包
	ID_CREAT_ROOM_REQ = 1009,                   //创建房间请求
	ID_CREAT_ROOM_RSP = 1010,                   //创建房间回包
	ID_JOIN_ROOM_REQ = 1011,                    //加入房间请求
	ID_JOIN_ROOM_RSP = 1012,                    //加入房间回包
	ID_GET_OSS_TOKEN = 1013,                    //获取OSS上传签名
	ID_UPLOAD_IMAGE = 1014,                     //上传图片到OSS
	ID_SAVE_IMAGE = 1015,                       //将图片保存到数据库
};
#define MAX_LENGTH 1024*2

class Defer
{
public:
	Defer(std::function<void()> func) :_func(std::move(func)) {};

	//作用域结束自动调用
	~Defer()
	{
		if (_func)
			_func();	//执行清理逻辑
	}

	// 禁止拷贝（避免重复执行）
	Defer(const Defer&) = delete;
	Defer& operator=(const Defer&) = delete;
private:
	std::function<void()> _func;	//存储延迟执行的函数
};

// 定义房间信息结构体
struct RoomInfo {
	std::string id;
	std::string name;
	int owner_uid;
	std::string host; // CanvasServer IP
	int port;         // CanvasServer Port
	int width = 1920;
	int height = 1080;
};

#define CODEPREFIX "code_"
#define TOKEN_PREFIX "utoken_"
#define UID_PREFIX "uid_token_" //用于通过 uid 找 token
#define ROOM_PREFIX "canvas:room:"
#define ROOM_USERS_PREFIX "room_users:"
