const config_module = require("./config")
const Redis = require('ioredis');

/**
 * 创建 Redis 客户端
 */
const RedisCli = new Redis({
    host: config_module.redis_host,
    port: config_module.redis_port,
    password: config_module.redis_pass,
});

/**
 * 监听错误信息
 */
RedisCli.on("error", function (err) {
    console.log("RedisCli connect error");
    RedisCli.quit();
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
        console.log('Result:', '<' + result + '>', 'Get key success!...');
        return result
    } catch (error) {
        console.log('GetRedis error is ', error);
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
        console.log('Result:', '<' + result + '>', 'Get key success!...');
        return result
    } catch (error) {
        console.log('QueryRedis error is ', error);
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
        await RedisCli.set(key, value)
        await RedisCli.expire(key, exptime);
        return true;
    } catch (error) {
        console.log('SetRedisExpire error is ', error);
        return false;
    }
}

/**
 * 退出函数
 */
function Quit() {
    RedisCli.quit();
}

module.exports = { GetRedis, QueryRedis, Quit, SetRedisExpire }