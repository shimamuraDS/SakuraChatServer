//
// Created by adachi on 26-3-5.
//

#include "CSession.h"
#include "CServer.h"
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cstdint>
#include <limits>
#include <algorithm>

#include "LogicSystem.h"

CSession::CSession(boost::asio::io_context& io_context, CServer* server) : _strand(boost::asio::make_strand(io_context)), _socket(_strand), _loginDeadline(_strand), _readDeadline(_strand), _server(server), _b_close(false), _b_head_parse(false) {
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

void CSession::SetUserId(int uid) {
    _user_id = uid;
}

int CSession::GetUserId() {
    return _user_id;
}

void CSession::Start() {
    if (!_strand.running_in_this_thread()) {
        boost::asio::dispatch(_strand, [self = SharedSelf()] { self->Start(); });
        return;
    }
    if (IsClosed()) return;
    _loginDeadline.expires_after(std::chrono::seconds(15));
    _loginDeadline.async_wait([self = SharedSelf()](const boost::system::error_code& ec) {
        if (!ec && self->GetUserId() <= 0) self->Close();
    });
    AsyncReadHead(HEAD_TOTAL_LEN);
}

void CSession::Close() {
    if (_b_close.exchange(true)) return;
    boost::asio::dispatch(_strand, [self = SharedSelf()] {
        boost::system::error_code ignored;
        self->_loginDeadline.cancel();
        self->_readDeadline.cancel();
        self->_socket.close(ignored);
        self->_server->ClearSession(self->_session_id);
        // Keep pending write buffers alive until the cancelled completion runs.
    });
}

void CSession::ArmReadDeadline(std::chrono::seconds timeout) {
    const auto generation = ++_readGeneration;
    _readDeadline.expires_after(timeout);
    _readDeadline.async_wait([self = SharedSelf(), generation](const boost::system::error_code& ec) {
        if (!ec && generation == self->_readGeneration) self->Close();
    });
}

bool CSession::AcceptPacket() {
    const auto now = std::chrono::steady_clock::now();
    _packetBudget = (std::min)(80.0, _packetBudget + std::chrono::duration<double>(now - _budgetAt).count() * 40.0);
    _budgetAt = now;
    if (_packetBudget < 1.0) { Close(); return false; }
    _packetBudget -= 1.0;
    return true;
}

void CSession::Send(std::string msg, short msgid) {
    if (IsClosed()) return;
    if (msg.empty() || msg.size() > std::numeric_limits<short>::max() - HEAD_TOTAL_LEN) {
        Close();
        return;
    }
    if (!_strand.running_in_this_thread()) {
        if (_scheduledSends.fetch_add(1) >= MAX_SENDQUE) {
            --_scheduledSends;
            Close();
            return;
        }
        boost::asio::dispatch(_strand, [self = SharedSelf(), msg = std::move(msg), msgid]() mutable {
            --self->_scheduledSends;
            self->Send(std::move(msg), msgid);
        });
        return;
    }
    std::lock_guard<std::mutex> lock(_send_lock);
    int send_que_size = _send_que.size();
    if (send_que_size >= MAX_SENDQUE) {
        Close();
        return;
    }

    _send_que.push(std::make_shared<SendNode>(msg.c_str(), static_cast<short>(msg.length()), msgid));
    if (send_que_size > 0) {
        return;
    }
    auto& msgnode = _send_que.front();
    boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len), std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
}

void CSession::Send(char* msg, short max_length, short msgid) {
    if (!msg || max_length <= 0 || max_length > std::numeric_limits<short>::max() - HEAD_TOTAL_LEN) {
        Close();
        return;
    }
    Send(std::string(msg, static_cast<std::size_t>(max_length)), msgid);
}

// 异步读取消息头，读取完成后会调用回调函数处理读取结果
void CSession::AsyncReadHead(int total_len) {
    if (IsClosed()) return;
    ArmReadDeadline(std::chrono::seconds(120));
    auto self = shared_from_this();
    asyncReadFull(HEAD_TOTAL_LEN, [self, this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
        try {
            if (ec || IsClosed()) {
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

            std::uint16_t msg_id = 0;
            memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
            msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
            std::cout << "msg_id is " << msg_id << std::endl;

            // 验证消息ID是否合法
            if (msg_id == 0 || msg_id > std::numeric_limits<short>::max()) {
                std::cout << "invalid msg_id is " << msg_id << std::endl;
                Close();
                _server->ClearSession(_session_id);
                return;
            }

            std::uint16_t msg_len = 0;
            memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
            // 网络字节序转换为主机字节序
            msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);
            std::cout << "msg_len is " << msg_len << std::endl;

            // 验证消息长度是否合法
            if (msg_len == 0 || msg_len > MAX_LENGTH) {
                std::cout << "invalid msg_len is " << msg_len << std::endl;
                Close();
                _server->ClearSession(_session_id);
                return;
            }

            _recv_msg_node = std::make_shared<RecvNode>(msg_len, msg_id);
            AsyncReadBody(msg_len);
        } catch (std::exception& e) {
            std::cout << "exception is " << e.what() << std::endl;
        }
    });
}

void CSession::AsyncReadBody(int total_len) {
    if (IsClosed()) return;
    ArmReadDeadline(std::chrono::seconds(15));
    auto self = shared_from_this();
    asyncReadFull(total_len, [self, this, total_len](const boost::system::error_code& ec, std::size_t bytes_transferred) {
        try {
            if (ec || IsClosed()) {
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
            // Never log raw packets: login tokens and message text are private.

            if (!AcceptPacket()) return;
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
    if (maxLength == 0 || maxLength > sizeof(_data)) {
        handler(boost::asio::error::invalid_argument, 0);
        return;
    }
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
    if (IsClosed()) return;
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

LogicNode::LogicNode(std::shared_ptr<CSession>  session,
    std::shared_ptr<RecvNode> recvnode):_session(session),_recvnode(recvnode) {

}
