//
// Created by adachi on 26-3-5.
//

#ifndef CSERVER_H
#define CSERVER_H
#include "const.h"
#include "AsioIOServicePool.h"
#include "CSession.h"
#include "UserMgr.h"

class CServer : public std::enable_shared_from_this<CServer> {
public:
    CServer(boost::asio::io_context& io_context, short port);
    ~CServer();
    void ClearSession(std::string);
private:
    void HandleAccept(std::shared_ptr<CSession>, const boost::system::error_code& error);
    void StartAccept();
    boost::asio::io_context& _io_context;
    short _port;
    tcp::acceptor _acceptor;
    std::map<std::string, std::shared_ptr<CSession>> _sessions;
    std::mutex _mutex;
};



#endif //CSERVER_H
