//
// Created by adachi on 25-8-13.
//

#include "RedisMgr.h"
#include "ConfigMgr.h"
#include <algorithm>

namespace {
// The linked hiredis version dereferences null in freeReplyObject.
void FreeReplyIfPresent(redisReply* reply) {
    if (reply != nullptr) freeReplyObject(reply);
}
}

// Redis连接池
RedisConPool::RedisConPool(size_t poolSize, const char* host, int port, const char* pwd)
    : _poolSize(poolSize), _host(host), _port(port), _b_stop(false) {
    for (size_t i = 0; i < poolSize; i++) {
        timeval timeout{3, 0};
        auto* context = redisConnectWithTimeout(host, port, timeout);
        if (context == nullptr || context->err != 0) {
            if (context != nullptr) {
                redisFree(context);
            }
            continue;
        }

        redisSetTimeout(context, timeout);
        auto reply = (redisReply*)redisCommand(context, "AUTH %s", pwd);
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            std::cout << "认证失败" << std::endl;
            FreeReplyIfPresent(reply);
            redisFree(context);
            continue;
        }

        FreeReplyIfPresent(reply);
        std::cout << "认证成功" << std::endl;
        _connections.push(context);
    }
}

RedisConPool::~RedisConPool() {
    std::lock_guard<std::mutex> lock(_mutex);
    while (!_connections.empty()) {
        redisFree(_connections.front());
        _connections.pop();
    }
}

redisContext* RedisConPool::getConnection() {
    std::unique_lock<std::mutex> lock(_mutex);
    _cond.wait_for(lock, std::chrono::seconds(3), [this]() {
        if (_b_stop) {
            return true;
        }
        return !_connections.empty();
    });
    if (_b_stop || _connections.empty()) {
        return nullptr;
    }
    auto* context = _connections.front();
    _connections.pop();
    return context;
}

void RedisConPool::returnConnction(redisContext* context) {
    if (!context) return;
    std::lock_guard<std::mutex> lock(_mutex);
    if (_b_stop) {
        redisFree(context);
        return;
    }
    _connections.push(context);
    _cond.notify_one();
}

void RedisConPool::Close() {
    _b_stop = true;
    _cond.notify_all();
}

// Redis管理
RedisMgr::RedisMgr() {
    auto& gCfgMgr = ConfigMgr::Inst();
    auto host = gCfgMgr["Redis"]["Host"];
    auto port = gCfgMgr["Redis"]["Port"];
    auto pwd = gCfgMgr["Redis"]["Password"];
    _con_pool.reset(new RedisConPool(5, host.c_str(), atoi(port.c_str()), pwd.c_str()));
}

RedisMgr::~RedisMgr() {
    Close();
}

bool RedisMgr::Get(const std::string& key, std::string& value) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    // 归还连接
    Defer defer([this, &connect]() {
        _con_pool->returnConnction(connect);
    });

    auto reply = (redisReply*) redisCommand(connect, "GET %s", key.c_str());
    if (reply == NULL) {
        std::cout << "[ GET " << key << " ] failed" << std::endl;
        FreeReplyIfPresent(reply);
        return false;
    }
    if (reply->type != REDIS_REPLY_STRING || reply->str == nullptr) {
        std::cout << "[ GET " << key << " ] failed" << std::endl;
        FreeReplyIfPresent(reply);
        return false;
    }

    value = reply->str;
    FreeReplyIfPresent(reply);

    std::cout << "Succeed to execute command [ GET " << key << " ]" << std::endl;
    return true;
}

bool RedisMgr::Set(const std::string& key, const std::string& value) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    // 归还连接
    Defer defer([this, &connect]() {
        _con_pool->returnConnction(connect);
    });

    auto reply = (redisReply*)redisCommand(connect, "SET %s %s", key.c_str(), value.c_str());
    if (reply == NULL) {
        std::cerr << "Redis SET failed" << std::endl;
        FreeReplyIfPresent(reply);
        return false;
    }

    if (reply->type != REDIS_REPLY_STATUS || !reply->str || strcmp(reply->str, "OK") != 0) {
        std::cerr << "Redis SET rejected" << std::endl;
        FreeReplyIfPresent(reply);
        return false;
    }

    FreeReplyIfPresent(reply);
    return true;
}

bool RedisMgr::SetEx(const std::string& key, const std::string& value, int seconds) {
    if (seconds <= 0) return false;
    auto connection = _con_pool->getConnection();
    if (!connection) return false;
    Defer giveBack([&] { _con_pool->returnConnction(connection); });
    auto reply = static_cast<redisReply*>(redisCommand(connection, "SET %b %b EX %d",
        key.data(), key.size(), value.data(), value.size(), seconds));
    if (!reply) return false;
    const bool ok = reply->type == REDIS_REPLY_STATUS && reply->str && std::string(reply->str) == "OK";
    FreeReplyIfPresent(reply);
    return ok;
}

bool RedisMgr::ConsumeCode(const std::string& key, const std::string& code) {
    if (code.size() != 6) return false;
    auto connection = _con_pool->getConnection();
    if (!connection) return false;
    Defer giveBack([&] { _con_pool->returnConnction(connection); });
    const char* script = "local v=redis.call('GET',KEYS[1]); if not v then return 0 end; "
        "local n=redis.call('INCR',KEYS[2]); if n==1 then redis.call('EXPIRE',KEYS[2],180) end; "
        "if n>5 then redis.call('DEL',KEYS[1]); return 0 end; "
        "if v~=ARGV[1] then return 0 end; redis.call('DEL',KEYS[1]); return 1";
    const auto attempts = key + ":attempts";
    auto reply = static_cast<redisReply*>(redisCommand(connection, "EVAL %s 2 %b %b %b", script,
        key.data(), key.size(), attempts.data(), attempts.size(), code.data(), code.size()));
    if (!reply) return false;
    const bool ok = reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    FreeReplyIfPresent(reply);
    return ok;
}

bool RedisMgr::AllowRequest(const std::string& key, int limit, int seconds) {
    return CheckRateLimit(key, limit, seconds).state == RateLimitResult::State::Allowed;
}

RateLimitResult RedisMgr::CheckRateLimit(const std::string& key, int limit, int seconds) {
    if (limit <= 0 || seconds <= 0) return {};
    auto connection = _con_pool->getConnection();
    if (!connection) return {};
    Defer giveBack([&] { _con_pool->returnConnction(connection); });
    const char* script = "local n=redis.call('INCR',KEYS[1]); local t=redis.call('TTL',KEYS[1]); "
        "if t<0 then redis.call('EXPIRE',KEYS[1],ARGV[1]); t=tonumber(ARGV[1]) end; return {n,t}";
    auto reply = static_cast<redisReply*>(redisCommand(connection, "EVAL %s 1 %b %d", script, key.data(), key.size(), seconds));
    if (!reply) return {};
    RateLimitResult result;
    if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 2 && reply->element[0] && reply->element[1]
        && reply->element[0]->type == REDIS_REPLY_INTEGER && reply->element[1]->type == REDIS_REPLY_INTEGER) {
        result.state = reply->element[0]->integer <= limit ? RateLimitResult::State::Allowed : RateLimitResult::State::Limited;
        result.retryAfter = static_cast<int>(std::max<long long>(1, std::min<long long>(seconds, reply->element[1]->integer)));
    }
    FreeReplyIfPresent(reply); return result;
}

bool RedisMgr::DeleteIfEqual(const std::string& key, const std::string& value) {
    auto connection = _con_pool->getConnection();
    if (!connection) return false;
    Defer giveBack([&] { _con_pool->returnConnction(connection); });
    const char* script = "if redis.call('GET',KEYS[1])==ARGV[1] then return redis.call('DEL',KEYS[1]) end; return 0";
    auto reply = static_cast<redisReply*>(redisCommand(connection, "EVAL %s 1 %b %b", script, key.data(), key.size(), value.data(), value.size()));
    if (!reply) return false;
    const bool ok = reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    FreeReplyIfPresent(reply); return ok;
}

bool RedisMgr::Auth(const std::string& password) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    // 归还连接
    Defer defer([this, &connect]() {
        _con_pool->returnConnction(connect);
    });

    auto reply = (redisReply*)redisCommand(connect, "AUTH %s", password.c_str());
    if (!reply) return false;
    if (reply->type != REDIS_REPLY_STATUS || !reply->str || std::string(reply->str) != "OK") {
        std::cout << "认证失败" << std::endl;
        FreeReplyIfPresent(reply);
        return false;
    } else {
        FreeReplyIfPresent(reply);
        std::cout << "认证成功" << std::endl;
        return true;
    }
}

bool RedisMgr::LPush(const std::string& key, const std::string& value) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }
    // 归还连接
    Defer defer([this, &connect]() {
        _con_pool->returnConnction(connect);
    });
    auto reply = (redisReply*)redisCommand(connect, "LPush %s %s", key.c_str(), value.c_str());
    if (reply == NULL) {
        FreeReplyIfPresent(reply);
        return false;
    }
    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
        FreeReplyIfPresent(reply);
        return false;
    }

    FreeReplyIfPresent(reply);
    return true;
}

bool RedisMgr::LPop(const std::string& key, std::string& value) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }
    // 归还连接
    Defer defer([this, &connect]() {
        _con_pool->returnConnction(connect);
    });
    auto reply = (redisReply*)redisCommand(connect, "LPop %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_STRING || reply->str == nullptr) {
        std::cout << "Execute command [ LPop " << key << " ] failure !" << std::endl;
        FreeReplyIfPresent(reply);
        return false;
    }
    value = reply->str;
    std::cout << "Execute command [ LPop " << key << " ] success !" << std::endl;
    FreeReplyIfPresent(reply);
    return true;
}

bool RedisMgr::RPush(const std::string& key, const std::string& value) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }
    // 归还连接
    Defer defer([this, &connect]() {
        _con_pool->returnConnction(connect);
    });
    auto reply = (redisReply*)redisCommand(connect, "RPUSH %s %s", key.c_str(), value.c_str());
    if (NULL == reply)
    {
        FreeReplyIfPresent(reply);
        return false;
    }

    if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
        FreeReplyIfPresent(reply);
        return false;
    }

    FreeReplyIfPresent(reply);
    return true;
}

bool RedisMgr::RPop(const std::string& key, std::string& value) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }
    // 归还连接
    Defer defer([this, &connect]() {
        _con_pool->returnConnction(connect);
    });
    auto reply = (redisReply*)redisCommand(connect, "RPOP %s ", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_STRING || reply->str == nullptr) {
        std::cout << "Execut command [ RPOP " << key << " ] failure ! " << std::endl;
        FreeReplyIfPresent(reply);
        return false;
    }
    value = reply->str;
    std::cout << "Execut command [ RPOP " << key << " ] success ! " << std::endl;
    FreeReplyIfPresent(reply);
    return true;
}

bool RedisMgr::HSet(const std::string& key, const std::string& hkey, const std::string& value) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }
    // 归还连接
    Defer defer([this, &connect]() {
        _con_pool->returnConnction(connect);
    });
    auto reply = (redisReply*)redisCommand(connect, "HSET %s %s %s", key.c_str(), hkey.c_str(), value.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER) {
        FreeReplyIfPresent(reply);
        return false;
    }
    FreeReplyIfPresent(reply);
    return true;
}

bool RedisMgr::HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }
    // 归还连接
    Defer defer([this, &connect]() {
        _con_pool->returnConnction(connect);
    });
    const char* argv[4];
    size_t argvlen[4];
    argv[0] = "HSET";
    argvlen[0] = 4;
    argv[1] = key;
    argvlen[1] = strlen(key);
    argv[2] = hkey;
    argvlen[2] = strlen(hkey);
    argv[3] = hvalue;
    argvlen[3] = hvaluelen;
    auto reply = (redisReply*)redisCommandArgv(connect, 4, argv, argvlen);
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER) {
        FreeReplyIfPresent(reply);
        return false;
    }
    FreeReplyIfPresent(reply);
    return true;
}

bool RedisMgr::HDel(const std::string& key, std::string& field) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }

    Defer defer([this, &connect]() {
        _con_pool->returnConnction(connect);
    });

    redisReply *reply = (redisReply*)redisCommand(connect, "HDEL %s %s", key.c_str(), field.c_str());
    if (reply == nullptr) {
        std::cerr << "HDEL command failed" << std::endl;
        return false;
    }

    bool success = false;
    if (reply->type != REDIS_REPLY_INTEGER) {
        success = reply->integer > 0;
    }
    FreeReplyIfPresent(reply);
    return success;
}

std::string RedisMgr::HGet(const std::string& key, const std::string& heky) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return "";
    }
    // 归还连接
    Defer defer([this, &connect]() {
        _con_pool->returnConnction(connect);
    });
    const char* argv[3];
    size_t argvlen[3];
    argv[0] = "HGET";
    argvlen[0] = 4;
    argv[1] = key.c_str();
    argvlen[1] = key.length();
    argv[2] = heky.c_str();
    argvlen[2] = heky.length();
    auto reply = (redisReply*)redisCommandArgv(connect, 3, argv, argvlen);
    if (reply == nullptr || reply->type != REDIS_REPLY_STRING || reply->str == nullptr) {
        FreeReplyIfPresent(reply);
        std::cout << "Execute command [ HGet " << key << " " << heky << " ] failure ! " << std::endl;
        return "";
    }
    std::string value = reply->str;
    FreeReplyIfPresent(reply);
    std::cout << "Execute command [ HGet " << key << " " << heky << " ] success ! " << std::endl;
    return value;
}

bool RedisMgr::Del(const std::string& key) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }
    // 归还连接
    Defer defer([this, &connect]() {
        _con_pool->returnConnction(connect);
    });
    auto reply = (redisReply*)redisCommand(connect, "DEL %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER) {
        std::cout << "Execute command [ Del " << key << " ] failure !" << std::endl;
        FreeReplyIfPresent(reply);
        return false;
    }
    std::cout << "Execute command [ Del " << key << " ] success !" << std::endl;
    FreeReplyIfPresent(reply);
    return true;
}

bool RedisMgr::ExistsKey(const std::string& key) {
    auto connect = _con_pool->getConnection();
    if (connect == nullptr) {
        return false;
    }
    // 归还连接
    Defer defer([this, &connect]() {
        _con_pool->returnConnction(connect);
    });
    auto reply = (redisReply*)redisCommand(connect, "exists %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER || reply->integer == 0) {
        std::cout << "Not Found [ Key " << key << " ]  ! " << std::endl;
        FreeReplyIfPresent(reply);
        return false;
    }
    std::cout << " Found [ Key " << key << " ] exists ! " << std::endl;
    FreeReplyIfPresent(reply);
    return true;
}

void RedisMgr::Close() {
    _con_pool->Close();
}





