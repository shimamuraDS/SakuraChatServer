#include "LogicSystem.h"
#include "HttpConnection.h"
#include "VerifyGrpcClient.h"
#include "StatusGrpcClient.h"
#include "RedisMgr.h"
#include "MysqlMgr.h"
#include "PasswordSecurity.h"
#include "PrivateChatHttp.h"
#include <algorithm>

namespace {
bool field(const Json::Value &r, const char *name, size_t max) {
    return r[name].isString() && !r[name].asString().empty() && r[name].asString().size() <= max &&
           r[name].asString().find('\0') == std::string::npos;
}
}
void LogicSystem::RegGet(std::string url, HttpHandler handler) { _get_handlers.emplace(std::move(url), std::move(handler)); }
void LogicSystem::RegPost(std::string url, HttpHandler handler) { _post_handlers.emplace(std::move(url), std::move(handler)); }
LogicSystem::LogicSystem() {
    RegGet("/healthz", [](std::shared_ptr<HttpConnection> connection) {
        connection->_response.set(http::field::content_type, "application/json");
        connection->_response.set(http::field::cache_control, "no-store");
        beast::ostream(connection->_response.body()) << "{\"status\":\"alive\"}";
    });
    RegPost("/private/v1", [](std::shared_ptr<HttpConnection> connection) {
        boost::system::error_code ec;
        const auto remote = connection->_socket.remote_endpoint(ec);
        auto result = PrivateChatHttp::Handle(beast::buffers_to_string(connection->_request.body().data()),
                                              ec ? std::string() : remote.address().to_string());
        connection->_response.set(http::field::content_type, "application/json; charset=utf-8");
        connection->_response.set(http::field::cache_control, "no-store");
        Json::StreamWriterBuilder writer; writer["indentation"] = "";
        beast::ostream(connection->_response.body()) << Json::writeString(writer, result);
    });
    for (const auto path : {"/get_varifycode", "/user_register", "/reset_pwd", "/user_login"}) {
        RegPost(path, [path](std::shared_ptr<HttpConnection> connection) {
            Json::Value response; response["error"] = ErrorCodes::Error_Json;
            Defer respond([&] {
                connection->_response.set(http::field::content_type, "application/json; charset=utf-8");
                connection->_response.set(http::field::cache_control, "no-store");
                beast::ostream(connection->_response.body()) << response.toStyledString();
            });
            try {
                Json::Value request; Json::Reader reader;
                const auto body = beast::buffers_to_string(connection->_request.body().data());
                if (body.size() > 16 * 1024 || !reader.parse(body, request) || !request.isObject() ||
                    !field(request, "email", 254)) return;
                std::string email = request["email"].asString();
                if (email.find('@') == std::string::npos ||
                    !std::all_of(email.begin(), email.end(), [](unsigned char c) { return c > 32 && c < 127; })) return;
                boost::system::error_code ec;
                const auto remote = connection->_socket.remote_endpoint(ec);
                if (ec) return;
                auto redis = RedisMgr::GetInstance();
                // Never trust arbitrary X-Forwarded-For; a proxy needs explicit trusted-peer setup.
                if (!redis->AllowRequest("limit:gate:global", 200, 60) ||
                    !redis->AllowRequest("limit:gate:ip:" + remote.address().to_string(), 30, 60) ||
                    !redis->AllowRequest(std::string("limit:gate:") + path + ":" + PasswordSecurity::Digest(email), 5, 60)) {
                    response["error"] = ErrorCodes::RPCFailed;
                    response["message"] = "请求过于频繁或服务暂不可用，请稍后再试";
                    return;
                }
                const std::string route(path);
                if (route == "/get_varifycode") {
                    if (!field(request, "purpose", 16)) return;
                    auto purpose = request["purpose"].asString();
                    if (purpose != "register" && purpose != "reset") return;
                    const auto rpc = VerifyGrpcClient::GetInstance()->GetVarifyCode(email, purpose);
                    response["error"] = rpc.error();
                    return;
                }
                const char *passwordKey = route == "/user_register" ? "password" : "passwd";
                if (!field(request, passwordKey, 128)) return;
                const auto password = request[passwordKey].asString();
                auto mysql = MysqlMgr::GetInstance();
                if (route == "/user_login") {
                    UserInfo user;
                    if (!mysql->CheckPwd(email, password, user)) {
                        response["error"] = ErrorCodes::PasswdInvalid;
                        response["message"] = "邮箱或密码不正确，旧账号请先重置密码";
                        return;
                    }
                    const auto rpc = StatusGrpcClient::GetInstance()->GetChatServer(user.uid);
                    response["error"] = rpc.error();
                    if (rpc.error()) return;
                    response["uid"] = user.uid; response["email"] = email;
                    response["token"] = rpc.token(); response["host"] = rpc.host(); response["port"] = rpc.port();
                    return;
                }
                if (!PasswordSecurity::Valid(password) || !field(request, "varifycode", 6)) {
                    response["message"] = "密码需为 8～128 字节，验证码为六位数字";
                    return;
                }
                const auto code = request["varifycode"].asString();
                const bool registering = route == "/user_register";
                const auto nameKey = registering ? "username" : "user";
                if (!field(request, nameKey, 64)) return;
                const auto name = request[nameKey].asString();
                if (registering && (!request["confirm"].isString() || request["confirm"].asString() != password)) return;
                const auto codeKey = std::string(CODEPREFIX) + (registering ? "register:" : "reset:") + email;
                if (!redis->ConsumeCode(codeKey, code)) {
                    response["error"] = ErrorCodes::VarifyCodeErr;
                    response["message"] = "验证码无效或已过期，请重新获取";
                    return;
                }
                if (registering) {
                    const int uid = mysql->RegUser(name, email, password);
                    response["error"] = uid > 0 ? ErrorCodes::Success : ErrorCodes::UserExist;
                    if (uid > 0) response["uid"] = uid;
                } else {
                    const auto user = mysql->GetUser(name);
                    if (!user || user->email != email) { response["error"] = ErrorCodes::PasswdUpFailed; return; }
                    // Invalidate existing sessions before changing the credential.
                    if (!redis->SetEx(std::string(USERTOKENPREFIX) + std::to_string(user->uid), PasswordSecurity::Token(), 1)) {
                        response["error"] = ErrorCodes::RPCFailed; return;
                    }
                    response["error"] = mysql->UpdatePwd(name, password) ? ErrorCodes::Success : ErrorCodes::PasswdUpFailed;
                }
            } catch (...) {
                response.clear(); response["error"] = ErrorCodes::RPCFailed;
                std::cerr << "Gateway operation failed" << std::endl;
            }
        });
    }
}
bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> connection) {
    const auto it = _get_handlers.find(path); if (it == _get_handlers.end()) return false;
    it->second(std::move(connection)); return true;
}
bool LogicSystem::HandlePost(std::string path, std::shared_ptr<HttpConnection> connection) {
    const auto it = _post_handlers.find(path); if (it == _post_handlers.end()) return false;
    it->second(std::move(connection)); return true;
}
