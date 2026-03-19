//
// Created by adachi on 26-3-5.
//

#include "CServer.h"

CServer::CServer(boost::asio::io_context& io_context, short port) : _io_context(io_context), _port(port), _acceptor(io_context, tcp::endpoint(tcp::v4(), port)) {
    std::cout << "Server start success, listen on port: " << _port << std::endl;
    StartAccept();
}

CServer::~CServer() {
}

void CServer::ClearSession(std::string session_id) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_sessions.find(session_id) != _sessions.end()) {
        auto uid = _sessions[session_id]->GetUserId();
        UserMgr::GetInstance()->RmvUserSession(uid, session_id);
    }

    _sessions.erase(session_id);
}

void CServer::HandleAccept(std::shared_ptr<CSession> new_session, const boost::system::error_code& error) {
    if (!error) {
        new_session->Start();
        std::lock_guard<std::mutex> lock(_mutex);
        _sessions.insert(std::make_pair(new_session->GetSessionId(), new_session));
        std::cout << "New connection accepted" << std::endl;
    } else {
        std::cout << "Accept error: " << error.message() << std::endl;
    }
}

void CServer::StartAccept() {
    auto &io_context = AsioIOServicePool::GetInstance()->GetIOService();
    std::shared_ptr<CSession> new_session = std::make_shared<CSession>(io_context, this);
    _acceptor.async_accept(new_session->GetSocket(), std::bind(&CServer::HandleAccept, this, new_session, std::placeholders::_1));
}
