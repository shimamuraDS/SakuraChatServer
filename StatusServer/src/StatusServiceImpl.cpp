//
// Created by adachi on 25-11-26.
//

#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "PasswordSecurity.h"
#include "RpcSecurity.h"
#include "RedisMgr.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

std::string generate_unique_string() {
    // 创建UID对象
    boost::uuids::uuid uuid = boost::uuids::random_generator()();

    // 将UUID转换为字符串
    std::string unique_string = to_string(uuid);
    return unique_string;
}

StatusServiceImpl::StatusServiceImpl() {
    auto& cfg = ConfigMgr::Inst();
    auto server_list = cfg["ChatServers"]["Name"];

    std::vector<std::string> words;
    std::stringstream ss(server_list);
    std::string word;

    while (std::getline(ss, word, ',')) {
        words.push_back(word);
    }

    for (auto& word : words) {
        if (cfg[word]["Name"].empty()) {
            continue;
        }
        ChatServer server;
        server.port = cfg[word]["Port"];
        server.host = cfg[word]["Host"];
        server.name = cfg[word]["Name"];
        _servers[server.name] = server;
    }
}

bool StatusServiceImpl::insertToken(int uid, std::string token) {
    std::string uid_str = std::to_string(uid);
    std::string token_key = std::string(USERTOKENPREFIX) + uid_str;
    return RedisMgr::GetInstance()->SetEx(token_key, PasswordSecurity::Digest(token), 12 * 60 * 60);
}

Status StatusServiceImpl::GetChatServer(ServerContext* context, const GetChatServerReq* request,
                                        GetChatServerRsp* reply) {
    if (!RpcSecurity::Allowed(context, "sakura-gate")) return Status(grpc::StatusCode::PERMISSION_DENIED, "Service identity required");
    std::string prefix("sakura status server has received : ");
    const auto& server = getChatServer();
    if (server.name.empty()) {
        reply->set_error(ErrorCodes::RPCFailed);
        return Status::OK;
    }
    reply->set_host(server.host);
    reply->set_port(server.port);
    reply->set_error(ErrorCodes::Success);
    if (request->uid() <= 0) return Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid account");
    reply->set_token(PasswordSecurity::Token());
    if (!insertToken(request->uid(), reply->token())) {
        reply->clear_token();
        reply->set_error(ErrorCodes::RPCFailed);
    }
    return Status::OK;
}

Status StatusServiceImpl::Login(ServerContext* context, const LoginReq* request, LoginRsp* reply) {
    if (!RpcSecurity::Allowed(context, "sakura-chat")) return Status(grpc::StatusCode::PERMISSION_DENIED, "Service identity required");
    auto uid = request->uid();
    auto token = request->token();
    // std::lock_guard<std::mutex> guard(_token_mtx);
    // auto iter = _tokens.find(uid);
    // if (iter == _tokens.end()) {
    //     reply->set_error(ErrorCodes::UidInvalid);
    //     return Status::OK;
    // }
    // if (iter->second != token) {
    //     reply->set_error(ErrorCodes::TokenInvalid);
    //     return Status::OK;
    // }
    std::string uid_str = std::to_string(uid);
    std::string token_key = std::string(USERTOKENPREFIX) + uid_str;
    std::string token_value = "";
    bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
    if (!success) {
        reply->set_error(ErrorCodes::UidInvalid);
        return Status::OK;
    }
    if (token_value != PasswordSecurity::Digest(token)) {
        reply->set_error(ErrorCodes::TokenInvalid);
        return Status::OK;
    }

    reply->set_error(ErrorCodes::Success);
    reply->set_uid(uid);
    reply->set_token(token);
    return Status::OK;
}

ChatServer StatusServiceImpl::getChatServer() {
    std::lock_guard<std::mutex> guard(_server_mtx);
    ChatServer minServer{};
    minServer.con_count = INT_MAX;
    for (auto& server : _servers) {
        auto count_str = RedisMgr::GetInstance()->HGet(std::string(LOGIN_COUNT), server.second.name);
        // A stopped node removes its registration; do not route logins to it.
        if (count_str.empty()) continue;
        try {
            size_t consumed = 0;
            server.second.con_count = std::stoi(count_str, &consumed);
            if (consumed != count_str.size() || server.second.con_count < 0) continue;
        } catch (const std::exception&) { continue; }
        if (server.second.con_count < minServer.con_count) {
            minServer = server.second;
        }
    }

    return minServer;
}

