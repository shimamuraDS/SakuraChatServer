#include "PrivateChatWire.h"
#include <cstdio>
#include <cstdlib>
static void check(bool ok, const char *message) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }
static Json::Value bytes(int count) { Json::Value value(Json::arrayValue); while (count--) value.append(5); return value; }
int main() {
    Json::Value request;
    request["op"] = "register"; request["request_id"] = "d987030a-e41c-4fde-bca9-71c2ce109111"; request["identity"] = bytes(33);
    check(PrivateChatWire::valid(request), "register");
    request["state"] = "private key"; check(!PrivateChatWire::valid(request), "reject private state"); request.removeMember("state");
    request["op"] = "publish";
    auto &bundle = request["bundle"];
    bundle["identity"] = bytes(33); bundle["prekey"] = bytes(33); bundle["signed"] = bytes(33);
    bundle["signature"] = bytes(64); bundle["kyber"] = bytes(1569); bundle["kyber_signature"] = bytes(64);
    bundle["registration"] = 1; bundle["id"] = 1;
    check(PrivateChatWire::valid(request), "public prekey bundle");
    bundle["private"] = bytes(32); check(!PrivateChatWire::valid(request), "no private prekey allowed");
    request.removeMember("bundle"); request["op"] = "send"; request["peer"] = 2;
    request["peer_identity"] = bytes(33); request["id"] = request["request_id"]; request["kind"] = 3; request["ciphertext"] = "AQID";
    check(PrivateChatWire::valid(request), "ciphertext envelope");
    request["plaintext"] = "message"; check(!PrivateChatWire::valid(request), "reject plaintext field"); request.removeMember("plaintext");
    request["ciphertext"] = std::string(90000, 'A'); check(!PrivateChatWire::valid(request), "oversized envelope");
    std::puts("Private wire validation tests passed");
}
