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

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

enum ErrorCodes {
    Success = 0,
    Error_Json = 1001,
    RPCFailed = 1002,
};

#endif //CONST_H
