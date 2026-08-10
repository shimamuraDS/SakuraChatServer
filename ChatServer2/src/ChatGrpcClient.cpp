//
// Created by adachi on 26-4-28.
//

#include "ChatGrpcClient.h"
#include "ConfigMgr.h"

ChatConPool::ChatConPool(size_t poolSize, std::string host, std::string port):
    _poolSize(poolSize), _host(host), _port(port), _b_stop(false) {
    for (size_t i = 0; i < _poolSize; i++) {
        std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(host + ":" + port, grpc::InsecureChannelCredentials());
        _connections.push(message::ChatService::NewStub(channel));
    }
}

ChatConPool::~ChatConPool() {
    std::lock_guard<std::mutex> lock(_mutex);
    Close();
    while (!_connections.empty()) {
        _connections.pop();
    }
}

std::unique_ptr<message::ChatService::Stub> ChatConPool::getConnection() {
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

void ChatConPool::returnConnection(std::unique_ptr<message::ChatService::Stub> context) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_b_stop) {
        return;
    }
    _connections.push(std::move(context));
    _cond.notify_one();
}

void ChatConPool::Close() {
    _b_stop = true;
    _cond.notify_all();
}

ChatGrpcClient::~ChatGrpcClient() {
}

message::AddFriendRsp ChatGrpcClient::NotifyAddFriend(std::string server_ip, const message::AddFriendReq& req) {
    message::AddFriendRsp rsp;
    return rsp;
}

message::AuthFriendRsp ChatGrpcClient::NotifyAuthFriend(std::string server_ip, const message::AuthFriendReq& req) {
    message::AuthFriendRsp rsp;
    return rsp;
}

bool ChatGrpcClient::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& user_info) {
    return true;
}

message::TextChatMsgRsp ChatGrpcClient::NotifyTextChatMsg(std::string server_ip, const message::TextChatMsgReq& req,
    const Json::Value& rtvalue) {
    message::TextChatMsgRsp rsp;
    return rsp;
}

ChatGrpcClient::ChatGrpcClient() {
    auto& cfg = ConfigMgr::Inst();
    auto server_list = cfg["PeerServer"]["Servers"];

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
        _pools[cfg[word]["Name"]] = std::make_unique<ChatConPool>(5, cfg[word]["Host"], cfg[word]["Port"]);
    }

}
