const config_module = require("./config")
const Redis = require('ioredis');

/**
 * 创建 Redis 客户端
 */
const RedisCli = new Redis({
    host: config_module.redis_host,
    port: config_module.redis_port,
    password: config_module.redis_pass,
    connectTimeout: 5000,
    commandTimeout: 5000,
    maxRetriesPerRequest: 1,
    enableOfflineQueue: false,
});

/**
 * 监听错误信息
 */
RedisCli.on("error", function (err) {
    console.log("RedisCli connect error");
    // Let ioredis reconnect; commands fail promptly while disconnected.
});

/**
 * 根据key获取value
 * @param {*} key
 * @returns
 */
async function GetRedis(key) {
    try {
        const result = await RedisCli.get(key)
        if (result === null) {
            console.log('result:', '<' + result + '>', 'This key cannot be find...');
            return null
        }
        return result
    } catch (error) {
        console.error('Redis GET failed');
        return null
    }
}

/**
 * 根据key查询redis中是否存在key
 * @param {*} key
 * @returns
 */
async function QueryRedis(key) {
    try {
        const result = await RedisCli.exists(key)
        if (result === 0) {
            console.log('result:', '<' + result + '>', 'This key cannot be find...');
            return null
        }
        return result
    } catch (error) {
        console.error('Redis EXISTS failed');
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
async function SetRedisExpire(key, value, exptime) {
    try {
        await RedisCli.set(key, value, 'EX', exptime)
        return true;
    } catch (error) {
        console.error('Redis SET failed');
        return false;
    }
}

/**
 * 退出函数
 */
function Quit() {
    RedisCli.quit();
}

async function ReserveCode(key, code) {
    const script = "if redis.call('EXISTS',KEYS[2])==1 then return 0 end; " +
        "local n=redis.call('INCR',KEYS[3]); if n==1 then redis.call('EXPIRE',KEYS[3],60) end; " +
        "if n>100 then return 0 end; redis.call('SET',KEYS[2],'1','EX',60); " +
        "redis.call('SET',KEYS[1],ARGV[1],'EX',180); redis.call('DEL',KEYS[4]); return 1";
    return await RedisCli.eval(script, 4, key, key + ':cooldown', 'limit:mail:global', key + ':attempts', code) === 1;
}
module.exports = { GetRedis, QueryRedis, Quit, SetRedisExpire, ReserveCode }
