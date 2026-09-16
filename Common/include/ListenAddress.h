#pragma once
#include <boost/asio/ip/address.hpp>
#include <cstdlib>
#include <string>

// Explicit container setting; desktop deployments retain their existing bind address.
inline std::string ListenHost(const std::string &fallback) {
    const char *host = std::getenv("SAKURA_LISTEN_ADDRESS");
    if (!host) return fallback;
    return boost::asio::ip::make_address(host).to_string();
}
