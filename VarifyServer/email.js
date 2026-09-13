const nodemailer = require('nodemailer');
const config_moudle = require("./config")

// 创建发送邮件的代理
let transport = nodemailer.createTransport({
    host: config_moudle.email_host,
    port: 465,
    secure: true,
    tls: { rejectUnauthorized: true, minVersion: 'TLSv1.2' },
    connectionTimeout: 10000,
    greetingTimeout: 10000,
    socketTimeout: 15000,
    auth: {
        user: config_moudle.email_user,
        pass: config_moudle.email_pass
    }
});

/**
 * 发送邮件
 * @param {*} mailOptions_ 发送邮件的参数
 * @returns
 */
function SendMail(mailOptions_) {
    return new Promise(function (resolve, reject) {
        transport.sendMail(mailOptions_, function (error, info) {
            if (error) {
                console.error('SMTP delivery failed');
                reject(new Error('SMTP delivery failed'));
            } else {
                resolve(true)
            }
        });
    })
}

module.exports.SendMail = SendMail
