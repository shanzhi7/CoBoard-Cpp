const config_module = require('./config');
const Redis = require('ioredis');

//创建redis客户端实例
const RedisCli = new Redis({
    host: config_module.redis_host,
    port:config_module.redis_port,
    password: config_module.redis_passwd
});

/*
    监听错误信息
*/
RedisCli.on("error",function (err)
{
    console.log("RedisCli connect error");
    RedisCli.quit();
});

/** 
 * 根据key获取value
 * @param {*} key 
 * @returns 
*/
async function GetRedis(key)
{
    try{
        const result = await RedisCli.get(key)
        if(result == null)
        {
            console.log('result:','<'+result+'>','这个key: '+key+' 值没有找到...')
            return null
        }
        console.log('Result:','<'+result+'>','Get key 成功!...');
        return result
    }catch(error){
        console.log('GetRedis error is',error);
        return null;
    }
}
/**
 * 根据key查询redis中是否存在key
 * @param {*} key 
 * @returns 
 */
async function QueryRedis(key)
{
    try{
        const result = await RedisCli.exists(key)
        //判断该值是否为空
        if(result == null)
        {
            console.log('result:<','<'+result+'>','不存在这个key: '+key+' ...');
            return null
        }
        console.log('Result:','<'+result+'>','存在这个key: '+key+' ...');
        return result
    }catch(error){
        console.log('QueryRedis error is', error);
        return null
    }
}
/**
 * 设置key和value，并设置过期时间
 * @param {*} key 
 * @param {*} value 
 * @param {*} exptime 
 * @returns 
 */
async function SetRedisExpire(key,value,exptime)
{
    try{
        //设置键值
        await RedisCli.set(key,value);
        //设置过期时间 (以秒为单位)
        await RedisCli.expire(key,exptime);
        return true;
    }catch(error)
    {
        console.log('SetRedisExpire error is', error);
        return false;
    }
}
/**
 * 退出函数
 */
function Quit()
{
    RedisCli.quit();
}
module.exports = {GetRedis,QueryRedis,Quit,SetRedisExpire}