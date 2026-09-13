const fs = require('fs');

const mode = process.env.SAKURA_SECURITY_MODE ?? 'development'
if (!['development', 'production'].includes(mode)) throw new Error('Invalid SAKURA_SECURITY_MODE')
const production = mode === 'production'
let config
if (process.env.SAKURA_CONFIG_PATH === '') throw new Error('Empty SAKURA_CONFIG_PATH')
try { config = JSON.parse(fs.readFileSync(process.env.SAKURA_CONFIG_PATH || 'config.json', 'utf8')) }
catch (_) { throw new Error('Cannot read configuration JSON; check path and syntax') }
function setting(name, fallback, required = false) {
    const provided = Object.prototype.hasOwnProperty.call(process.env, name)
    const value = provided ? process.env[name] : fallback
    if (typeof value !== 'string' || (provided && !value.length) || (required && !value.length))
        throw new Error('Missing or invalid environment variable: ' + name)
    return value
}
function secret(name, fallback) { return setting(name, production ? '' : (fallback || ''), true) }
function port(name, fallback) {
    const value = setting(name, String(fallback || ''))
    if (!/^[0-9]+$/.test(value) || Number(value) < 1 || Number(value) > 65535)
        throw new Error('Invalid port: ' + name)
    return Number(value)
}
const email_user = secret('SAKURA_SMTP_USER', config.email?.user)
const email_pass = secret('SAKURA_SMTP_PASSWORD', config.email?.pass)
const email_host = setting('SAKURA_SMTP_HOST', 'smtp.qq.com', true)
const redis_host = setting('SAKURA_REDIS_HOST', config.redis?.host || '127.0.0.1', true)
const redis_port = port('SAKURA_REDIS_PORT', config.redis?.port || 6379)
const redis_pass = secret('SAKURA_REDIS_PASSWORD', config.redis?.password)
// Verification does not use MySQL; never export its credentials.
module.exports = { email_user, email_pass, email_host, redis_host, redis_port, redis_pass, code_prefix: 'code_' }
