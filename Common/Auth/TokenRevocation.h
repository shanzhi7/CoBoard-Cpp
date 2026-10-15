/***********************************************************************************
* @file         TokenRevocation.h
* @brief        账号 Token 原子撤销脚本，保护新登录凭据
* @author       shanzhi
* @date         2026/10/14
* @history
***********************************************************************************/
#pragma once

inline const char* TokenRevocationScript() // 返回仅撤销指定登录凭据的 Redis 脚本。
{
    // 1. Token 必须属于请求账号；缺失表示已经撤销，UID 索引只允许条件删除。
    return R"lua(
local uid = redis.call('GET', KEYS[1])
if uid and uid ~= ARGV[1] then return -1 end
if uid then redis.call('DEL', KEYS[1]) end
if redis.call('GET', KEYS[2]) == ARGV[2] then redis.call('DEL', KEYS[2]) end
return 1
)lua";
}
