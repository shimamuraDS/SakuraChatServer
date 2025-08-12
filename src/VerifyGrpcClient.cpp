//
// Created by adachi on 25-8-8.
//

#include "VerifyGrpcClient.h"
#include "AsioIOServicePool.h"
#include "ConfigMgr.h"

RPConPool::RPConPool(size_t poolsize, std::string host, std::string port):
_poolSize(poolsize), _host(host), _port(port), _b_stop(false) {
    for (size_t i = 0; i < _poolSize; i++) {
        std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port, grpc::InsecureChannelCredentials());
        _connections.push(VarifyService::NewStub(channel));
    }
}

RPConPool::~RPConPool() {
    std::lock_guard<std::mutex> lock(_mutex);
    Close();
    while (!_connections.empty()) {
        _connections.pop();
    }
}

void RPConPool::Close() {
    _b_stop = true;
    _cond.notify_all();
}

std::unique_ptr<VarifyService::Stub> RPConPool::getConnection()  {
    std::unique_lock<std::mutex> lock(_mutex);
    _cond.wait(lock,[this]() {
        if (_b_stop) {
            return true;
        }
        return !_connections.empty();
    });

    if (_b_stop) {
        return nullptr;
    }

    auto context = std::move(_connections.front());
    _connections.pop();
    return context;
}

void RPConPool::returnConnection(std::unique_ptr<VarifyService::Stub> context) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_b_stop) {
        return;
    }

    _connections.push(std::move(context));
    _cond.notify_one();
}


GetVarifyRsp VerifyGrpcClient::GetVarifyCode(std::string email) {
    ClientContext context;
    GetVarifyRsp reply;
    GetVarifyReq request;
    request.set_email(email);

    auto stub = _pool->getConnection();
    Status status = stub->GetVarifyCode(&context, request, &reply);
    if (status.ok()) {
        _pool->returnConnection(std::move(stub));
        return reply;
    } else {
        _pool->returnConnection(std::move(stub));
        std::cerr << "gRPC call failed: " << status.error_message() << std::endl;
        reply.set_error(ErrorCodes::RPCFailed);
        return reply;
    }
}

VerifyGrpcClient::VerifyGrpcClient() {
    auto & gCfgMgr = ConfigMgr::Inst();
    std::string host = gCfgMgr["VarifyServer"]["Host"];
    std::string port = gCfgMgr["VarifyServer"]["Port"];
    _pool.reset(new RPConPool(5, host, port));
}
