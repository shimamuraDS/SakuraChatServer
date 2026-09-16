//
// Created by adachi on 25-8-4.
//

#include "HttpConnection.h"
#include "LogicSystem.h"
#include <iostream>
#include <stdexcept>

HttpConnection::HttpConnection(net::io_context& ioc): _socket(ioc) {
}

// 启动HTTP连接，开始异步读取请求
void HttpConnection::Start() {
    auto self = shared_from_this();
    _parser.body_limit(96 * 1024);
    _parser.header_limit(8 * 1024);
    _deadline.expires_after(std::chrono::seconds(15));
    CheckDeadline();
    http::async_read(_socket, _buffer, _parser, [self](beast::error_code ec, std::size_t bytes_transferred) {
        try {
            if (ec) {
                std::cout << "http read err is " << ec.what() << std::endl;
                beast::error_code ignored;
                self->_socket.close(ignored);
                self->_deadline.cancel();
                return;
            }

            boost::ignore_unused(bytes_transferred);
            self->_request = self->_parser.release();
            self->HandleRequest();
        }
        catch (std::exception& e) {
            std::cerr << "HTTP request rejected" << std::endl;
            beast::error_code ignored;
            self->_socket.close(ignored);
            self->_deadline.cancel();
        }
    });
}

unsigned char ToHex(unsigned char x) {
    return x < 10 ? x + '0' : x - 10 + 'A';
}

unsigned char FromHex(unsigned char x) {
    unsigned char y;
    if (x >= 'A' && x <= 'F') y = x - 'A' + 10;
    else if (x >= 'a' && x <= 'f') y = x - 'a' + 10;
    else if (x >= '0' && x <= '9') y = x - '0';
    else
        throw std::invalid_argument("invalid URL escape");
    return y;
}

// URL编码
std::string UrlEncode(const std::string& str) {
    std::string strTemp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++) {
        if (isalnum((unsigned char)str[i]) ||
            (str[i] == '-') ||
            (str[i] == '_') ||
            (str[i] == '.') ||
            (str[i] == '~'))
            strTemp += str[i];
        else if (str[i] == ' ')
            strTemp += "+";
        else {
            strTemp += '%';
            strTemp += ToHex((unsigned char)str[i] >> 4);
            strTemp += ToHex((unsigned char)str[i] & 0x0F);
        }
    }
    return strTemp;
}

// URL解码
std::string UrlDecode(const std::string& str) {
    std::string strTemp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++) {
        if (str[i] == '+') strTemp += ' ';
        else if (str[i] == '%') {
            if (length - i < 3) throw std::invalid_argument("incomplete URL escape");
            unsigned char high = FromHex((unsigned char)str[++i]);
            unsigned char low = FromHex((unsigned char)str[++i]);
            strTemp += high * 16 + low;
        }
        else strTemp += str[i];
    }
    return strTemp;
}

void HttpConnection::PreParseGetParam() {
    // 提取URI
    auto uri = _request.target();
    // 查找字符串的开始位置（即‘？’的位置）
    auto query_pos = uri.find('?');
    if (query_pos == std::string::npos) {
        _get_url = uri;
        return;
    }

    _get_url = uri.substr(0, query_pos);
    std::string query_string = uri.substr(query_pos + 1);
    std::string key;
    std::string value;
    size_t pos = 0;
    while ((pos = query_string.find('&')) != std::string::npos) {
        auto pair = query_string.substr(0, pos);
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            key = UrlDecode(pair.substr(0, eq_pos));
            value = UrlDecode(pair.substr(eq_pos + 1));
            _get_params[key] = value;
        }
        query_string.erase(0, pos + 1);
    }
    // 处理最后一个参数
    if (!query_string.empty()) {
        size_t eq_pos = query_string.find('=');
        if (eq_pos != std::string::npos) {
            key = UrlDecode(query_string.substr(0, eq_pos));
            value = UrlDecode(query_string.substr(eq_pos + 1));
            _get_params[key] = value;
        }
    }
}


// 处理HTTP请求
void HttpConnection::HandleRequest() {
    // 设置版本
    _response.version(_request.version());
    _response.keep_alive(false);

    // 处理GET请求
    if (_request.method() == http::verb::get) {
        try { PreParseGetParam(); }
        catch (const std::invalid_argument &) {
            _response.result(http::status::bad_request);
            _response.set(http::field::content_type, "text/plain");
            beast::ostream(_response.body()) << "Invalid request target";
            WriteResponse();
            return;
        }
        bool success = LogicSystem::GetInstance()->HandleGet(_get_url, shared_from_this());
        if (!success) {
            _response.result(http::status::not_found);
            _response.set(http::field::content_type, "text/plain");
            beast::ostream(_response.body()) << "url not found\r\n";
            WriteResponse();
            return;
        }
        _response.result(http::status::ok);
        _response.set(http::field::server, "GateServer");
        WriteResponse();
        return;
    }

    // 处理POST请求
    if (_request.method() == http::verb::post) {
        bool success = LogicSystem::GetInstance()->HandlePost(_request.target(), shared_from_this());
        if (!success) {
            _response.result(http::status::not_found);
            _response.set(http::field::content_type, "text/plain");
            beast::ostream(_response.body()) << "url not found\r\n";
            WriteResponse();
            return;
        }
        _response.result(http::status::ok);
        _response.set(http::field::server, "GateServer");
        WriteResponse();
        return;
    }

    _response.result(http::status::method_not_allowed);
    _response.set(http::field::allow, "GET, POST");
    WriteResponse();
}

// 发送HTTP响应
void HttpConnection::WriteResponse() {
    auto self = shared_from_this();
    _response.content_length(_response.body().size());
    http::async_write(_socket, _response, [self](beast::error_code ec, std::size_t) {
        self->_socket.shutdown(tcp::socket::shutdown_send, ec);
        self->_deadline.cancel();
    });
}

// 检查连接是否超时
void HttpConnection::CheckDeadline() {
    auto self = shared_from_this();
    _deadline.async_wait([self](beast::error_code ec) {
        if (!ec) {
            // 超时，关闭连接
            self->_socket.close(ec);
        }
    });
}
