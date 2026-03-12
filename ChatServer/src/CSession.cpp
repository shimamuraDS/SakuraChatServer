//
// Created by adachi on 26-3-5.
//

#include "CSession.h"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "LogicSystem.h"

CSession::CSession(boost::asio::io_context& io_context, CServer* server) : _socket(io_context), _server(server), _b_close(false), _b_head_parse(false) {
    boost::uuids::uuid a_uuid = boost::uuids::random_generator()();
    _session_id = boost::uuids::to_string(a_uuid);
    _recv_head_node = std::make_shared<MsgNode>(HEAD_TOTAL_LEN);
}

CSession::~CSession() {
    std::cout << "~CSession destruct" << std::endl;
}

tcp::socket& CSession::GetSocket() {
    return _socket;
}

std::string& CSession::GetSessionId() {
    return _session_id;
}

int CSession::GetUserId() {
    return _user_id;
}

void CSession::Start() {
    AsyncReadHead(HEAD_TOTAL_LEN);
}

void CSession::Close() {
    std::lock_guard<std::mutex> lock(_session_mtx);
    _socket.close();
    _b_close = true;
}

void CSession::Send(std::string msg, short msgid) {
    std::lock_guard<std::mutex> lock(_send_lock);
    int send_que_size = _send_que.size();
    if (send_que_size > MAX_SENDQUE) {
        std::cout << "session: " << _session_id << "send que is full, size is " << MAX_SENDQUE << std::endl;
        return;
    }

    _send_que.push(std::make_shared<SendNode>(msg.c_str(), msg.length(), msgid));
    if (send_que_size > 0) {
        return;
    }
    auto& msgnode = _send_que.front();
    boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len), std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
}

// 异步读取消息头，读取完成后会调用回调函数处理读取结果
void CSession::AsyncReadHead(int total_len) {
    auto self = shared_from_this();
    asyncReadFull(HEAD_TOTAL_LEN, [self, this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
        try {
            if (ec) {
                std::cout << "handle read failed, error is " << ec.what() << std::endl;
                Close();
                _server->ClearSession(_session_id);
                return;
            }

            if (bytes_transferred < HEAD_TOTAL_LEN) {
                std::cout << "read length not match, read [" << bytes_transferred << "], total [" << HEAD_TOTAL_LEN << "]" << std::endl;
                Close();
                _server->ClearSession(_session_id);
                return;
            }

            _recv_head_node->Clear();
            memcpy(_recv_head_node->_data, _data, bytes_transferred);

            short msg_id = 0;
            memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
            msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
            std::cout << "msg_id is " << msg_id << std::endl;

            // 验证消息ID是否合法
            if (msg_id > MAX_LENGTH) {
                std::cout << "invalid msg_id is " << msg_id << std::endl;
                _server->ClearSession(_session_id);
                return;
            }

            short msg_len = 0;
            memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
            // 网络字节序转换为主机字节序
            msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);
            std::cout << "msg_len is " << msg_len << std::endl;

            // 验证消息长度是否合法
            if (msg_len > MAX_LENGTH) {
                std::cout << "invalid msg_len is " << msg_len << std::endl;
                _server->ClearSession(_session_id);
                return;
            }

            _recv_msg_node = std::make_shared<RecvNode>(msg_len, msg_id);
            AsyncReadHead(msg_len);
        } catch (std::exception& e) {
            std::cout << "exception is " << e.what() << std::endl;
        }
    });
}

void CSession::AsyncReadBody(int total_len) {
    auto self = shared_from_this();
    asyncReadFull(total_len, [self, this, total_len](const boost::system::error_code& ec, std::size_t bytes_transferred) {
        try {
            if (ec) {
                std::cout << "handle read failed, error is " << ec.what() << std::endl;
                Close();
                _server->ClearSession(_session_id);
                return;
            }

            if (bytes_transferred < total_len) {
                std::cout << "read length not match, read [" << bytes_transferred << "], total [" << total_len << "]" << std::endl;
                Close();
                _server->ClearSession(_session_id);
                return;
            }

            memcpy(_recv_msg_node->_data, _data, bytes_transferred);
            _recv_msg_node->_cur_len += bytes_transferred;
            _recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
            std::cout << "receive data is " << _recv_msg_node->_data << std::endl;

            LogicSystem::GetInstance()->PostMsgToQue(std::make_shared<LogicNode>(shared_from_this(), _recv_msg_node));
            AsyncReadHead(HEAD_TOTAL_LEN);
        } catch (std::exception& e) {
            std::cout << "exception is " << e.what() << std::endl;
        }
    });
}

std::shared_ptr<CSession> CSession::SharedSelf() {
    return shared_from_this();
}

// 异步读取指定长度的数据，直到读取到指定长度或者发生错误
void CSession::asyncReadFull(std::size_t maxLength,
    std::function<void(const boost::system::error_code&, std::size_t)> handler) {
    ::memset(_data, 0, MAX_LENGTH);
    asyncReadLen(0, maxLength, handler);
}

// 递归异步读取，直到读取到指定长度或者发生错误
void CSession::asyncReadLen(std::size_t read_len, std::size_t total_len,
    std::function<void(const boost::system::error_code&, std::size_t)> handler) {
    auto self = shared_from_this();
    _socket.async_read_some(boost::asio::buffer(_data + read_len, total_len - read_len),
        [read_len, total_len, handler, self](const boost::system::error_code& ec, std::size_t bytesTransferred) {
            if (ec) {
                handler(ec, read_len + bytesTransferred);
                return;
            }

            if (read_len + bytesTransferred >= total_len) {
                handler(ec, read_len + bytesTransferred);
                return;
            }

            // 继续读取剩余数据
            self->asyncReadLen(read_len + bytesTransferred, total_len, handler);
        });
}

void CSession::HandleWrite(const boost::system::error_code& error, std::shared_ptr<CSession> shared_self) {
    try {
        if (!error) {
            std::lock_guard<std::mutex> lock(_send_lock);
            _send_que.pop();
            if (!_send_que.empty()) {
                auto& msgnode = _send_que.front();
                boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len), std::bind(&CSession::HandleWrite, this, std::placeholders::_1, shared_self));
            }
        } else {
            std::cout << "handle write failed, error is " << error.what() << std::endl;
            Close();
            _server->ClearSession(_session_id);
        }
    } catch (std::exception& e) {
        std::cerr << "exception is " << e.what() << std::endl;
    }
}


