//用来启动grpc server

const grpc = require('@grpc/grpc-js')
const message_proto = require('./proto')
const const_module = require('./const')
const {v4: uuidv4} = require('uuid')
const emailModule = require('./email')
const redis_module = require('./redis')

//获取验证码
async function GetVarifyCode(call, callback)    //声明async内部才能调用await，使SendMail函数同步
{
    console.log("email is ", call.request.email)
    try{
        let query_request = await redis_module.GetRedis(const_module.code_prefix + call.request.email);
        console.log("query_request is",query_request)
        let = uniqueId = query_request;
        if(query_request == null)
        {
             //获取验证码
            const uuid = uuidv4().replace(/-/g, ''); // 去除所有连字符
            uniqueId = uuid.substring(0, 6);  // 截取前6位

            //将验证码设置到redis里
            let bres = await redis_module.SetRedisExpire(const_module.code_prefix+call.request.email,uniqueId,60);
            if(!bres)
            {
                callback(null,{
                    email: call.request.email,
                    error: const_module.Error.RedisErr
                });
                return;
            }
        }
        console.log("uniqueId is ", uniqueId)
        let text_str =  'SyncCanvas 多人协作画板:\n 您的验证码为: '+ uniqueId +' 请一分钟内完成注册'

        //发送邮件
        let mailOptions = { //定义邮箱配置
            from: '2604585641@qq.com',
            to: call.request.email,
            subject: '验证码',
            text: text_str,
        };

        let send_res = await emailModule.SendMail(mailOptions);
        console.log("send res is ", send_res)

        callback(null, { email:  call.request.email,
            error:const_module.Error.Success
        }); 


    }catch(error){
        console.log("catch error is ", error)

        callback(null, { email:  call.request.email,
            error:const_module.Error.Exception
        }); 
    }

}

function main() {
    var server = new grpc.Server()
    server.addService(message_proto.VarifyService.service, { GetVarifyCode: GetVarifyCode })
    server.bindAsync('0.0.0.0:50057', grpc.ServerCredentials.createInsecure(), () => {  //更改此处端口号
        //server.start()
        console.log('grpc server started')        
    })
}

main()