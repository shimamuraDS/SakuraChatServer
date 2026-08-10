//
// Created by adachi on 25-8-13.
//

#ifndef REDISMGR_H
#define REDISMGR_H

#include <hiredis.h>
#include "const.h"

class RedisConPool {
public:
    RedisConPool(size_t poolSize, const char* host, int port, const char* pwd);
    ~RedisConPool();

    redisContext* getConnection();
    void returnConnction(redisContext* context);
    void Close();
private:
    std::atomic<bool> _b_stop;
    size_t _poolSize;
    const char* _host;
    int _port;
    std::queue<redisContext*> _connections;
    std::mutex _mutex;
    std::condition_variable _cond;
};

class RedisMgr : public Singleton<RedisMgr> {
    friend class Singleton<RedisMgr>;
public:
    ~RedisMgr();
    bool Get(const std::string& key, std::string& value);
    bool Set(const std::string& key, const std::string& value);
    bool Auth(const std::string& password);
    bool LPush(const std::string& key, const std::string& value);
    bool LPop(const std::string& key, std::string& value);
    bool RPush(const std::string& key, const std::string& value);
    bool RPop(const std::string& key, std::string& value);
    bool HSet(const std::string& key, const std::string& hkey, const std::string& value);
    bool HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen);
    bool HDel(const std::string& key, std::string& field);
    std::string HGet(const std::string &key, const std::string& heky);
    bool Del(const std::string& key);
    bool ExistsKey(const std::string& key);
    void Close();
private:
    RedisMgr();

    std::unique_ptr<RedisConPool> _con_pool;
};



#endif //REDISMGR_H
