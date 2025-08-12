//
// Created by adachi on 25-8-8.
//

#ifndef VERIFYGRPCCLIENT_H
#define VERIFYGRPCCLIENT_H

#include <grpcpp/grpcpp.h>
#include "Singleton.h"
#include "const.h"
#include "message.grpc.pb.h"

using grpc::ClientContext;
using grpc::Status;
using grpc::Channel;

using message::GetVarifyRsp;
using message::GetVarifyReq;
using message::VarifyService;

class RPConPool {
public:
    RPConPool(size_t poolsize, std::string host, std::string port);
    ~RPConPool();
    void Close();

    std::unique_ptr<VarifyService::Stub> getConnection();

    void returnConnection(std::unique_ptr<VarifyService::Stub> context);

private:
    std::atomic<bool> _b_stop;
    size_t _poolSize;
    std::string _host;
    std::string _port;
    std::queue<std::unique_ptr<VarifyService::Stub>> _connections;
    std::condition_variable _cond;
    std::mutex _mutex;
};

class VerifyGrpcClient:public Singleton<VerifyGrpcClient>{
    friend class Singleton<VerifyGrpcClient>;
public:
    GetVarifyRsp GetVarifyCode(std::string email);

private:
    VerifyGrpcClient();
    std::unique_ptr<VarifyService::Stub> _stub;
    std::unique_ptr<RPConPool> _pool;
};



#endif //VERIFYGRPCCLIENT_H
