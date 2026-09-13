const grpc = require('@grpc/grpc-js')
const fs = require('fs')
const { randomInt, timingSafeEqual } = require('crypto')
const message_proto = require('./proto')
const constants = require('./const')
const emailModule = require('./email')
const redis = require('./redis')
const production = process.env.SAKURA_SECURITY_MODE === 'production'
if (process.env.SAKURA_SECURITY_MODE && !['development', 'production'].includes(process.env.SAKURA_SECURITY_MODE))
    throw new Error('Invalid SAKURA_SECURITY_MODE')
const serviceKey = process.env.SAKURA_VERIFY_SERVICE_KEY || ''
if (production && serviceKey.length < 32) throw new Error('Missing SAKURA_VERIFY_SERVICE_KEY')
async function GetVarifyCode(call, callback) {
    try {
        if (production) {
            const received = Buffer.from(String(call.metadata.get('verify-service-key')[0] || ''))
            const expected = Buffer.from(serviceKey)
            if (received.length !== expected.length || !timingSafeEqual(received, expected))
                return callback({ code: grpc.status.PERMISSION_DENIED, details: 'Service identity required' })
        }
        const purpose = call.metadata.get('verify-purpose')[0]
        const email = call.request.email
        if (!['register', 'reset'].includes(purpose) || typeof email !== 'string' ||
            email.length > 254 || !/^[^\s@]+@[^\s@]+$/.test(email))
            return callback({ code: grpc.status.INVALID_ARGUMENT, details: 'Invalid request' })
        const code = String(randomInt(0, 1000000)).padStart(6, '0')
        const key = constants.code_prefix + purpose + ':' + email
        if (!await redis.ReserveCode(key, code))
            return callback(null, { email, error: constants.Errors.RedisErr })
        await emailModule.SendMail({ from: require('./config').email_user, to: email,
            subject: purpose === 'reset' ? '重置密码验证码' : '注册验证码',
            text: '您的验证码为 ' + code + '，3 分钟内有效。请勿向他人提供验证码。' })
        callback(null, { email, error: constants.Errors.Success })
    } catch (_) {
        console.error('Verification request failed')
        callback(null, { error: constants.Errors.Exception })
    }
}
function main() {
    const server = new grpc.Server({ 'grpc.max_receive_message_length': 4096 })
    server.addService(message_proto.VarifyService.service, { GetVarifyCode })
    let credentials = grpc.ServerCredentials.createInsecure()
    if (production) {
        const read = name => { if (!process.env[name]) throw new Error('Missing ' + name); return fs.readFileSync(process.env[name]) }
        credentials = grpc.ServerCredentials.createSsl(read('SAKURA_RPC_CA'),
            [{ private_key: read('SAKURA_RPC_KEY'), cert_chain: read('SAKURA_RPC_CERT') }], true)
    }
    const bind = production ? (process.env.SAKURA_VERIFY_BIND || '127.0.0.1:50051') : '127.0.0.1:50051'
    server.bindAsync(bind, credentials, error => { if (error) throw error; console.log('Verification service ready') })
}
main()
