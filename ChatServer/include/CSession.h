//
// Created by adachi on 26-3-5.
//

#ifndef CSESSION_H
#define CSESSION_H

#include "const.h"
#include "MsgNode.h"

class CServer;
class LogicSystem;

class CSession : public std::enable_shared_from_this<CSession> {
public:
    CSession(boost::asio::io_context& io_context, CServer* server);
    ~CSession();
    tcp::socket& GetSocket();
    std::string& GetSessionId();
    int GetUserId();
    void Start();
    void Close();
    void Send(std::string msg, short msgid);
    void Send(char* msg, short max_length, short msgid);
    void AsyncReadHead(int total_len);
    void AsyncReadBody(int total_len);
    std::shared_ptr<CSession> SharedSelf();
private:
    void asyncReadFull(std::size_t maxLength, std::function<void(const boost::system::error_code&, std::size_t)> handler);
    void asyncReadLen(std::size_t read_len, std::size_t total_len, std::function<void(const boost::system::error_code&, std::size_t)> handler);
    void HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self);
    tcp::socket _socket;
    CServer* _server;
    int _user_id;
    bool _b_close;
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
