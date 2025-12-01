//
// Created by adachi on 25-8-4.
//

#ifndef CONST_H
#define CONST_H

#include <memory>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <mutex>
#include <iostream>
#include "Singleton.h"
#include <map>
#include <json/json.h>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <cassert>
#include "message.grpc.pb.h"
#include "message.pb.h"

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

enum ErrorCodes {
    Success = 0,
    Error_Json = 1001, //Json解析错误
    RPCFailed = 1002, // RPC请求错误
    VarifyExpired = 1003, // 验证码过期
    VarifyCodeErr = 1004, // 验证码错误
    UserExist = 1005, // 用户已存在
    PasswdErr = 1006, // 密码错误
    EmailNotMatch = 1007, // 邮箱不匹配
    PasswdUpFailed = 1008, // 密码更新失败
    PasswdInvalid = 1009, // 密码更新失败
};

class Defer {
public:
    // 构造函数，接受一个可调用对象
    Defer(std::function<void()> func) : _func(func) {};

    // 析构函数，调用传入的可调用对象
    ~Defer() {
        _func();
    }

private:
    std::function<void()> _func;
};

#define CODEPREFIX "code_"

#endif //CONST_H
