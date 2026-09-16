#pragma once
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/network_v4.hpp>
#include <string>

inline std::string ClientAddress(const std::string& peer, const std::string& forwarded,
                                 const std::string& trustedNetwork) {
    if (trustedNetwork.empty()) return peer;
    boost::system::error_code error;
    const auto network = boost::asio::ip::make_network_v4(trustedNetwork, error);
    if (error) return peer;
    const auto source = boost::asio::ip::make_address(peer, error);
    if (error || !source.is_v4() ||
        (source.to_v4().to_uint() & network.netmask().to_uint()) != network.network().to_uint()) return peer;
    // The dedicated proxy overwrites this header; never accept an arbitrary chain.
    const auto address = boost::asio::ip::make_address(forwarded, error);
    return error ? std::string{} : address.to_string();
}
