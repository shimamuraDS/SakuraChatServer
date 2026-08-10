//
// Created by adachi on 26-4-28.
//

#ifndef CHATGRPYCLIENT_H
#define CHATGRPYCLIENT_H
#include <atomic>
#include <memory>
#include <queue>
#include <string>
#include <grpcpp/grpcpp.h>
#include "const.h"
#include "data.h"

class ChatConPool {
public:
    ChatConPool(size_t poolSize, std::string host, std::string port);
    ~ChatConPool();
    std::unique_ptr<message::ChatService::Stub> getConnection();
    void returnConnection(std::unique_ptr<message::ChatService::Stub> context);
    void Close();
private:
    std::atomic<bool> _b_stop;
    size_t _poolSize;
    std::string _host;
    std::string _port;
    std::queue<std::unique_ptr<message::ChatService::Stub>> _connections;
    std::mutex _mutex;
    std::condition_variable _cond;
};

class ChatGrpcClient : public Singleton<ChatGrpcClient> {
    friend class Singleton<ChatGrpcClient>;

public:
    ~ChatGrpcClient();
    message::AddFriendRsp NotifyAddFriend(std::string server_ip, const message::AddFriendReq& req);
    message::AuthFriendRsp NotifyAuthFriend(std::string server_ip, const message::AuthFriendReq& req);
    bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& user_info);
    message::TextChatMsgRsp NotifyTextChatMsg(std::string server_ip, const message::TextChatMsgReq& req, const Json::Value& rtvalue);
private:
    ChatGrpcClient();
    std::unordered_map<std::string, std::unique_ptr<ChatConPool>> _pools;
};



#endif //CHATGRPYCLIENT_H
