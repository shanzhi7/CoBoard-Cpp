//该文件用于读取config.json配置文件

//包含文件读写库

const fs = require('fs');

//解析config.json文件
let config = JSON.parse(fs.readFileSync('config.json','utf-8'));

let email_user = config.email.user;
let email_pass = config.email.pass;
let mysql_host = config.mysql.host;
let mysql_port = config.mysql.port;
let redis_host = config.redis.host;
let redis_port = config.redis.port;
let redis_passwd = config.redis.passwd;
let code_prefix = "code_";

//livekit配置
const livekit_url = config.livekit.url;
const livekit_api_key = config.livekit.api_key;
const livekit_api_secret = config.livekit.api_secret;

//导出内容
module.exports = {email_pass, email_user,
     mysql_host, mysql_port,redis_host,
     redis_port, redis_passwd, code_prefix,
     livekit_url,
     livekit_api_key,
     livekit_api_secret}