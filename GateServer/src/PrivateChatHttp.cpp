#include "PrivateChatHttp.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "PasswordSecurity.h"
#include "ChatWire.h"
#include <algorithm>

Json::Value PrivateChatHttp::Handle(const std::string &body, const std::string &remoteAddress) {
    Dependencies dependencies;
    dependencies.allowRequest = [](const std::string &key, int limit, int seconds) {
        return RedisMgr::GetInstance()->CheckRateLimit(key, limit, seconds);
    };
    dependencies.tokenDigest = [](int actor, std::string &digest) {
        return RedisMgr::GetInstance()->Get(std::string(USERTOKENPREFIX) + std::to_string(actor), digest);
    };
    dependencies.command = [](int actor, const Json::Value &request) {
        return MysqlMgr::GetInstance()->PrivateChatCommand(actor, request);
    };
    return Handle(body, remoteAddress, dependencies);
}

Json::Value PrivateChatHttp::Handle(const std::string &body, const std::string &remoteAddress, const Dependencies &dependencies) {
    Json::Value result; result["error"] = 1001;
    try {
        Json::Value request; Json::Reader reader;
        if (body.size() > 96 * 1024 || !reader.parse(body, request) || !request.isObject()
            || !request["uid"].isInt() || request["uid"].asInt() <= 0 || !request["token"].isString()
            || request["token"].asString().size() != 64) return result;
        if (request["request_id"].isString() && ChatWire::Uuid(request["request_id"].asString())) result["request_id"] = request["request_id"];
        const auto actor = request["uid"].asInt();
        auto allowed = [&](const std::string &key, int limit) {
            const auto decision = dependencies.allowRequest(key, limit, 60);
            if (decision.state == RateLimitResult::State::Allowed) return true;
            result["error"] = decision.state == RateLimitResult::State::Limited ? 1204 : 1207;
            result["retry_after"] = decision.state == RateLimitResult::State::Limited ? std::max(1, decision.retryAfter) : 5;
            return false;
        };
        if (remoteAddress.empty()) return result;
        if (!allowed("limit:private:global", 1000) || !allowed("limit:private:ip:" + remoteAddress, 300)) return result;
        std::string expected;
        const auto digest = PasswordSecurity::Digest(request["token"].asString());
        if (!dependencies.tokenDigest(actor, expected)
            || expected.size() != digest.size() || sodium_memcmp(expected.data(), digest.data(), digest.size()) != 0) {
            result["error"] = 1010; return result;
        }
        if (!allowed("limit:private:uid:" + std::to_string(actor), 120)) return result;
        if (request["op"] == "claim" && !allowed("limit:private:claim:" + std::to_string(actor), 10)) return result;
        request.removeMember("uid"); request.removeMember("token");
        result = dependencies.command(actor, request);
        if (request["request_id"].isString() && request["request_id"].asString().size() == 36) result["request_id"] = request["request_id"];
    } catch (...) { result["error"] = 1104; }
    return result;
}
