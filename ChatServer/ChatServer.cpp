//
// Created by adachi on 26-3-4.
//

#include <signal.h>
#include <grpcpp/server_builder.h>

#include "CServer.h"
#include "ConfigMgr.h"
#include "AsioIOServicePool.h"
#include "ChatServiceImpl.h"
#include "RedisMgr.h"
#include "RpcSecurity.h"
#include "ExpiryWorker.h"
#include "ListenAddress.h"

int main() {
    auto& cfg = ConfigMgr::Inst();
    auto server_name = cfg["SelfServer"]["Name"];
    try {
        cfg.RequireDatabaseCredentials();
        ExpiryWorker expiryWorker;
        auto pool = AsioIOServicePool::GetInstance();
        std::string server_address(ListenHost(cfg["SelfServer"]["Host"]) + ":" + cfg["SelfServer"]["RPCPort"]);
        if (RpcSecurity::Development()) server_address = "127.0.0.1:" + cfg["SelfServer"]["RPCPort"];
        ChatServiceImpl service;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(server_address, RpcSecurity::Server());
        builder.RegisterService(&service);
        std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
        if (!server) throw std::runtime_error("Cannot start Chat RPC server");
        std::cout << "RPC Server listening on " << server_address << std::endl;

        boost::asio::io_context io_context;
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context, pool, &server, &server_name](auto, auto) {
            RedisMgr::GetInstance()->HDel(std::string(LOGIN_COUNT), server_name);
            io_context.stop();
            pool->Stop();
            server->Shutdown();
        });
        auto port_str = cfg["SelfServer"]["Port"];
        CServer s(io_context, atoi(port_str.c_str()));
        if (!RedisMgr::GetInstance()->HSet(std::string(LOGIN_COUNT), server_name, "0"))
            throw std::runtime_error("Cannot register ChatServer");
        std::thread grpc_server_thread([&server]() { server->Wait(); });
        io_context.run();

        RedisMgr::GetInstance()->HDel(std::string(LOGIN_COUNT), server_name);
        RedisMgr::GetInstance()->Close();
        grpc_server_thread.join();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
