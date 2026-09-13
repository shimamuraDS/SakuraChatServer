#pragma once
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/auth_context.h>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <iterator>

namespace RpcSecurity {
inline bool Development() {
    const char* mode = std::getenv("SAKURA_SECURITY_MODE");
    if (!mode || std::string(mode) == "development") return true;
    if (std::string(mode) == "production") return false;
    throw std::runtime_error("Invalid SAKURA_SECURITY_MODE");
}
inline std::string Pem(const char *variable) {
    const char *path = std::getenv(variable);
    if (!path || !*path) throw std::runtime_error(std::string("Missing ") + variable);
    std::ifstream f(path, std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty()) throw std::runtime_error(std::string("Cannot read ") + variable);
    return data;
}
inline bool Local(const std::string &host) { return host == "127.0.0.1" || host == "localhost" || host == "::1"; }
inline std::shared_ptr<grpc::ChannelCredentials> Channel(const std::string &host) {
    if (Development()) {
        if (!Local(host)) throw std::runtime_error("Development RPC must use loopback");
        return grpc::InsecureChannelCredentials();
    }
    grpc::SslCredentialsOptions options;
    options.pem_root_certs = Pem("SAKURA_RPC_CA");
    options.pem_cert_chain = Pem("SAKURA_RPC_CERT");
    options.pem_private_key = Pem("SAKURA_RPC_KEY");
    return grpc::SslCredentials(options);
}
inline std::shared_ptr<grpc::ServerCredentials> Server() {
    if (Development()) return grpc::InsecureServerCredentials();
    grpc::SslServerCredentialsOptions options;
    options.pem_root_certs = Pem("SAKURA_RPC_CA");
    options.pem_key_cert_pairs.push_back({Pem("SAKURA_RPC_KEY"), Pem("SAKURA_RPC_CERT")});
    options.client_certificate_request = GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
    return grpc::SslServerCredentials(options);
}
inline bool Allowed(grpc::ServerContext *context, const std::string &identity) {
    if (Development()) {
        const auto peer = context->peer();
        return peer.rfind("ipv4:127.0.0.1:", 0) == 0 || peer.rfind("ipv6:[::1]:", 0) == 0;
    }
    auto auth = context->auth_context();
    if (!auth || !auth->IsPeerAuthenticated()) return false;
    for (const auto &value : auth->FindPropertyValues("x509_subject_alternative_name"))
        if (std::string(value.data(), value.size()) == identity) return true;
    return false;
}
}
