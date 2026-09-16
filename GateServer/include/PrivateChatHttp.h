#pragma once
#include <string>
#include <json/json.h>
#include <functional>
#include "RateLimitResult.h"
namespace PrivateChatHttp {
struct Dependencies {
    std::function<RateLimitResult(const std::string &, int, int)> allowRequest;
    std::function<bool(int, std::string &)> tokenDigest;
    std::function<Json::Value(int, const Json::Value &)> command;
};
Json::Value Handle(const std::string &body, const std::string &remoteAddress, const Dependencies &dependencies);
Json::Value Handle(const std::string &body, const std::string &remoteAddress);
}
