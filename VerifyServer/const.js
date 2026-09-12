//该文件用于定义全局公用内容
let code_prefix = "code_"

const Error = {
    Success : 0,
    RedisErr : 1,
    Exception : 2,
    VoiceTokenInvalidArgument : 1060,
    VoiceTokenConfigInvalid : 1061,
    VoiceTokenGenerateFailed : 1062,
    VoiceAuthFailed : 1063,
};

module.exports = {code_prefix,Error}
