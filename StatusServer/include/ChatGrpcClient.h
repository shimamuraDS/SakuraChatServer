//
// Created by adachi on 26-5-8.
//

#ifndef CHATGRPCCLIENT_H
#define CHATGRPCCLIENT_H
#include <atomic>
#include <memory>
#include <queue>
#include <string>
#include "const.h"

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
    message::AddFriendRsp NotifyAddFriend(const message::AddFriendReq& req);
private:
    ChatGrpcClient();
    std::unordered_map<std::string, std::unique_ptr<ChatConPool>> _pools;
};


#endif //CHATGRPCCLIENT_H
