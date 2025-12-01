//
// Created by adachi on 25-11-26.
//

#ifndef STATUSSERVICEIMPL_H
#define STATUSSERVICEIMPL_H

#include "const.h"
#include <string>

using grpc::Status;
using grpc::ServerContext;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::LoginReq;
using message::LoginRsp;

struct ChatServer {
    std::string host;
    std::string port;
    std::string name;
    int con_count;
};

class StatusServiceImpl final : public message::StatusService::Service {
public:
    StatusServiceImpl();
    Status GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply) override;
    // Status Login(ServerContext* context, const LoginReq* request, LoginRsp* reply) override;
private:
    void insertToken(int uid, std::string token);
    ChatServer getChatServer();
    std::unordered_map<std::string, ChatServer> _servers;
    std::mutex _server_mtx;
    std::unordered_map<int, std::string> _tokens;
    std::mutex _token_mtx;
};

#endif //STATUSSERVICEIMPL_H
