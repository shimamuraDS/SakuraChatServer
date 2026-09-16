//
// Created by adachi on 25-8-1.
//
#include "CServer.h"
#include "ConfigMgr.h"
#include <json/json.h>
#include <hiredis.h>
#include "RedisMgr.h"
#include "const.h"


int main() {
    try {
        auto & gCfgMgr = ConfigMgr::Inst();
        gCfgMgr.RequireDatabaseCredentials();
        const auto configuredPort = gCfgMgr["GateServer"]["Port"];
        if (configuredPort.empty() || configuredPort.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("Invalid GateServer Port");
        const auto parsedPort = std::stoul(configuredPort);
        if (parsedPort == 0 || parsedPort > 65535) throw std::runtime_error("Invalid GateServer Port");
        unsigned short port = static_cast<unsigned short>(parsedPort);
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
