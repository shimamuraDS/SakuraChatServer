//
// Created by adachi on 26-3-4.
//

#include <signal.h>
#include "ConfigMgr.h"
#include "CServer.h"
#include "AsioIOServicePool.h"

int main() {
    try {
        auto& cfg = ConfigMgr::Inst();
        auto pool = AsioIOServicePool::GetInstance();
        boost::asio::io_context io_context;
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context, pool](auto, auto) {
            io_context.stop();
            pool->Stop();
        });
        auto port_str = cfg["SelfServer"]["port"];
        CServer s(io_context, atoi(port_str.c_str()));
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}