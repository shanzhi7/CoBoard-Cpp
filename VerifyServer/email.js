//封装发邮件的模块

//包含发送邮件的库
const nodemailer = require('nodemailer');

//包含当前目录下的config.js
const config_module = require('./config');

//创建发送邮件的代理
let transport = nodemailer.createTransport({
    host: 'smtp.qq.com',
    port: 465,
    secure: true,   //启用 SSL 加密传输
    auth:{
        user: config_module.email_user, //邮箱发送者
        pass: config_module.email_pass  //邮箱发送者密码
    }
})

/**
 * 发送邮件的函数
 * @param {*} mailOptions_ 发送邮件的参数
 * @returns 
 */
function SendMail(mailOptions_){
    return new Promise(function(resolve, reject){
        transport.sendMail(mailOptions_, function(error, info){
            if (error) {
                console.log(error);
                reject(error);
            } else {
                console.log('邮件已成功发送：' + info.response);
                resolve(info.response)
            }
        });
    })

}

module.exports.SendMail = SendMail