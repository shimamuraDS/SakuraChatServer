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

int main() {
    auto& cfg = ConfigMgr::Inst();
    auto server_name = cfg["SelfServer"]["Name"];
    try {
        auto pool = AsioIOServicePool::GetInstance();
        // 设置登录数为0
        RedisMgr::GetInstance()->HSet(std::string(LOGIN_COUNT), server_name, "0");
        std::string server_address(cfg["SelfServer"]["Host"] + ":" + cfg["SelfServer"]["RPCPort"]);
        ChatServiceImpl service;
        grpc::ServerBuilder builder;
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service);
        std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
        std::cout << "RPC Server listening on " << server_address << std::endl;

        std::thread grpc_server_thread([&server]() {
            server->Wait();
        });

        boost::asio::io_context io_context;
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context, pool, &server](auto, auto) {
            io_context.stop();
            pool->Stop();
            server->Shutdown();
        });
        auto port_str = cfg["SelfServer"]["Port"];
        CServer s(io_context, atoi(port_str.c_str()));
        io_context.run();

        RedisMgr::GetInstance()->HDel(std::string(LOGIN_COUNT), server_name);
        RedisMgr::GetInstance()->Close();
        grpc_server_thread.join();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}
