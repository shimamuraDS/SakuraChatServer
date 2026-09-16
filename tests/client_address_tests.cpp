#include "ClientAddress.h"
#include <stdexcept>

int main() {
    auto check = [](const std::string& actual, const std::string& expected) {
        if (actual != expected) throw std::runtime_error("Unexpected client address");
    };
    check(ClientAddress("10.72.20.2", "198.51.100.1", ""), "10.72.20.2");
    check(ClientAddress("192.0.2.4", "198.51.100.1", "10.72.20.0/24"), "192.0.2.4");
    check(ClientAddress("10.72.20.2", "198.51.100.1", "10.72.20.0/24"), "198.51.100.1");
    check(ClientAddress("10.72.20.2", "2001:db8::1", "10.72.20.0/24"), "2001:db8::1");
    check(ClientAddress("10.72.20.2", "198.51.100.1, 10.0.0.1", "10.72.20.0/24"), "");
    check(ClientAddress("10.72.20.2", "", "10.72.20.0/24"), "");
    check(ClientAddress("10.72.20.2", "198.51.100.1", "invalid"), "10.72.20.2");
}
