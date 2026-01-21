#pragma once
#include <functional>

enum ErrorCodes
{
	Success = 0,
	Error_Json = 1001,		//Json解析错误
	RPCFailed = 1002,		//RPC请求错误
	VarifyExpired = 1003,	//验证码过期
	VarifyCodeErr = 1004,	//验证码错误
	UserExist = 1005,		//用户已经存在
	PasswdErr = 1006,		//密码错误
	EmailNotMatch = 1007,	//邮箱不匹配
	PasswdUpFailed = 1008,	//更新密码失败
	PasswdInvalid = 1009,	//密码更新失败
	TokenInvalid = 1010,	//token无效
	UidInvalid = 1011,		//uid无效
};

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
#define CODEPREFIX "code_"