//
// Created by adachi on 25-8-1.
//
#include <json/json.h>
#include "CServer.h"
#include "ConfigMgr.h"

int main() {
    ConfigMgr gCfgMgr;
    std::string gate_port_str = gCfgMgr["GateServer"]["port"];
    unsigned short gate_port = atoi(gate_port_str.c_str());

    try {
        unsigned short port = static_cast<unsigned short>(8081);
        net::io_context ioc{ 1 };
        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const boost::system::error_code &error, int sigbal_number) {
            if (error) {
                return;
            }
            ioc.stop();
        });

        std::make_shared<CServer>(ioc, port)->Start();
        std::cout << "Server is running on port " << port << std::endl;
        ioc.run();
    } catch (std::exception const& e) {
        std::cerr<<"Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}