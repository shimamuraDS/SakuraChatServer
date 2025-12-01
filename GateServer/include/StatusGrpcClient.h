//
// Created by adachi on 25-11-20.
//

#ifndef STATUSGRPCLIENT_H
#define STATUSGRPCLIENT_H

#include "ConfigMgr.h"

using message::StatusService;
using message::GetChatServerRsp;
using message::GetChatServerReq;

class StatusConPool {
public:
    StatusConPool(size_t poolsize, std::string host, std::string port);
    ~StatusConPool();

    std::unique_ptr<StatusService::Stub> getConnection();
    void returnConnection(std::unique_ptr<StatusService::Stub> context);
    void Close();

private:
    std::atomic<bool> _b_stop;
    size_t _poolSize;
    std::string _host;
    std::string _port;
    std::queue<std::unique_ptr<StatusService::Stub>> _connections;
    std::condition_variable _cond;
    std::mutex _mutex;
};

class StatusGrpcClient : public Singleton<StatusGrpcClient> {
    friend class Singleton<StatusGrpcClient>;

public:
    ~StatusGrpcClient() {

    }
    GetChatServerRsp GetChatServer(int uid);

private:
    StatusGrpcClient();
    std::unique_ptr<StatusConPool> _pool;
};


#endif //STATUSGRPCLIENT_H
