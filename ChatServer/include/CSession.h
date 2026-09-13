//
// Created by adachi on 26-3-5.
//

#ifndef CSESSION_H
#define CSESSION_H

#include "const.h"
#include "MsgNode.h"
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <cstdint>
#include <atomic>

class CServer;
class LogicSystem;

class CSession : public std::enable_shared_from_this<CSession> {
public:
    CSession(boost::asio::io_context& io_context, CServer* server);
    ~CSession();
    tcp::socket& GetSocket();
    std::string& GetSessionId();
    void SetUserId(int uid);
    int GetUserId();
    void SetAuthDigest(std::string digest) { _authDigest = std::move(digest); }
    const std::string &AuthDigest() const { return _authDigest; }
    void Start();
    void Close();
    bool IsClosed() const { return _b_close.load(); }
    void Send(std::string msg, short msgid);
    void Send(char* msg, short max_length, short msgid);
    void AsyncReadHead(int total_len);
    void AsyncReadBody(int total_len);
    std::shared_ptr<CSession> SharedSelf();
private:
    void ArmReadDeadline(std::chrono::seconds timeout);
    bool AcceptPacket();
    void asyncReadFull(std::size_t maxLength, std::function<void(const boost::system::error_code&, std::size_t)> handler);
    void asyncReadLen(std::size_t read_len, std::size_t total_len, std::function<void(const boost::system::error_code&, std::size_t)> handler);
    void HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self);
    boost::asio::strand<boost::asio::io_context::executor_type> _strand;
    tcp::socket _socket;
    boost::asio::steady_timer _loginDeadline;
    boost::asio::steady_timer _readDeadline;
    std::uint64_t _readGeneration = 0;
    double _packetBudget = 80;
    std::chrono::steady_clock::time_point _budgetAt = std::chrono::steady_clock::now();
    CServer* _server;
    std::atomic<int> _user_id{0};
    std::string _authDigest;
    std::atomic<bool> _b_close;
    std::atomic<int> _scheduledSends{0};
    bool _b_head_parse;
    std::mutex _send_lock;
    std::string _session_id;
    char _data[MAX_LENGTH];
    std::shared_ptr<MsgNode> _recv_head_node;
    std::shared_ptr<RecvNode> _recv_msg_node;
    std::queue<std::shared_ptr<SendNode>> _send_que;
    std::mutex _session_mtx;
};

class LogicNode {
    friend class LogicSystem;
public:
    LogicNode(std::shared_ptr<CSession>, std::shared_ptr<RecvNode>);
private:
    std::shared_ptr<CSession> _session;
    std::shared_ptr<RecvNode> _recvnode;
};

#endif //CSESSION_H
