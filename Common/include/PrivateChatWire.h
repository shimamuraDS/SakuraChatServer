#pragma once
#include <json/json.h>
#include <string>
#include <set>

namespace PrivateChatWire {
inline bool uuid(const std::string &id) {
    if (id.size() != 36) return false;
    for (unsigned i = 0; i < id.size(); ++i) {
        const char c = id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
        else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}
inline bool bytes(const Json::Value &value, unsigned length) {
    if (!value.isArray() || value.size() != length) return false;
    for (const auto &v : value) if (!v.isUInt() || v.asUInt() > 255) return false;
    return true;
}
inline bool bundle(const Json::Value &v) {
    const std::set<std::string> fields{"registration", "id", "identity", "prekey", "signed", "signature", "kyber", "kyber_signature"};
    if (!v.isObject() || v.size() != fields.size()) return false;
    for (const auto &name : v.getMemberNames()) if (!fields.count(name)) return false;
    return v["registration"].isUInt() && v["registration"].asUInt() > 0 && v["registration"].asUInt() < 16384
        && v["id"].isUInt() && v["id"].asUInt() > 0 && bytes(v["identity"], 33) && bytes(v["prekey"], 33)
        && bytes(v["signed"], 33) && bytes(v["signature"], 64) && bytes(v["kyber"], 1569) && bytes(v["kyber_signature"], 64);
}
inline bool valid(const Json::Value &r) {
    if (!r.isObject() || !r["op"].isString() || !r["request_id"].isString() || !uuid(r["request_id"].asString())) return false;
    const auto op = r["op"].asString();
    std::set<std::string> fields{"op", "request_id", "identity"};
    if (op == "register" || op == "poll") {}
    else if (op == "publish") fields.insert("bundle");
    else if (op == "identity" || op == "claim") fields.insert("peer");
    else if (op == "send") { fields.insert("peer"); fields.insert("peer_identity"); fields.insert("id"); fields.insert("kind"); fields.insert("ciphertext"); }
    else if (op == "ack") fields.insert("sequence");
    else return false;
    for (const auto &name : r.getMemberNames()) if (!fields.count(name)) return false;
    if (!bytes(r["identity"], 33)) return false;
    if (fields.count("peer") && (!r["peer"].isInt() || r["peer"].asInt() <= 0)) return false;
    if (op == "publish" && (!bundle(r["bundle"]) || r["bundle"]["identity"] != r["identity"])) return false;
    if (op == "send") {
        if (!bytes(r["peer_identity"], 33) || !r["id"].isString() || !uuid(r["id"].asString())
            || !r["kind"].isInt() || (r["kind"].asInt() != 2 && r["kind"].asInt() != 3) || !r["ciphertext"].isString()) return false;
        const auto &cipher = r["ciphertext"].asString();
        if (cipher.empty() || cipher.size() > 87384 || cipher.size() % 4 != 0) return false;
        for (char c : cipher) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=')) return false;
    }
    if (op == "ack" && (!r["sequence"].isString() || r["sequence"].asString().empty() || r["sequence"].asString().size() > 20
        || r["sequence"].asString().find_first_not_of("0123456789") != std::string::npos)) return false;
    return true;
}
}
