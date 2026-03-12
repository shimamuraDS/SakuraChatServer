//
// Created by adachi on 25-11-20.
//

#include "StatusGrpcClient.h"

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

using grpc::Channel;

StatusConPool::StatusConPool(size_t poolSize, std::string host, std::string port) : _poolSize(poolSize), _host(host), _port(port), _b_stop(false) {
    for (size_t i = 0; i < _poolSize; i++) {
        std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port, grpc::InsecureChannelCredentials());
        _connections.push(StatusService::NewStub(channel));
    }
}

StatusConPool::~StatusConPool() {
    std::lock_guard<std::mutex> lock(_mutex);
    Close();
    while (!_connections.empty()) {
        _connections.pop();
    }
}

std::unique_ptr<StatusService::Stub> StatusConPool::getConnection() {
    std::unique_lock<std::mutex> lock(_mutex);
    _cond.wait(lock, [this] {
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

void StatusConPool::returnConnection(std::unique_ptr<StatusService::Stub> context) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_b_stop) {
        return;
    }
    _connections.push(std::move(context));
    _cond.notify_one();
}

void StatusConPool::Close() {
    _b_stop = true;
    _cond.notify_all();
}

GetChatServerRsp StatusGrpcClient::GetChatServer(int uid) {
    grpc::ClientContext context;
    GetChatServerRsp reply;
    GetChatServerReq request;
    request.set_uid(uid);
    auto stub = _pool->getConnection();
    grpc::Status status = stub->GetChatServer(&context, request, &reply);
    Defer defer([&stub, this]() {
        _pool->returnConnection(std::move(stub));
    });
    if (status.ok()) {
        return reply;
    } else {
        reply.set_error(ErrorCodes::RPCFailed);
        return reply;
    }
}

StatusGrpcClient::StatusGrpcClient() {
    auto& gcfgMgr = ConfigMgr::Inst();
    std::string host = gcfgMgr["StatusServer"]["Host"];
    std::string port = gcfgMgr["StatusServer"]["Port"];
    _pool.reset(new StatusConPool(5, host, port));
}
