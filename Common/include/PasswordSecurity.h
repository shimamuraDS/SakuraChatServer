#pragma once
#include <sodium.h>
#include <string>
#include <stdexcept>

namespace PasswordSecurity {
inline void Init() {
    static const int initialized = sodium_init();
    if (initialized < 0) throw std::runtime_error("security initialization failed");
}
inline bool Valid(const std::string &password) {
    return password.size() >= 8 && password.size() <= 128 && password.find('\0') == std::string::npos;
}
inline std::string Hash(const std::string &password) {
    Init();
    if (!Valid(password)) throw std::invalid_argument("invalid password length");
    char encoded[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str_alg(encoded, password.data(), password.size(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
            crypto_pwhash_ALG_ARGON2ID13) != 0)
        throw std::runtime_error("password hashing unavailable");
    return encoded;
}
inline bool Verify(const std::string &encoded, const std::string &password) {
    Init();
    return password.size() <= 128 && encoded.rfind("$argon2id$", 0) == 0 &&
        crypto_pwhash_str_verify(encoded.c_str(), password.data(), password.size()) == 0;
}
inline std::string Token() {
    Init();
    unsigned char bytes[32]; char hex[65];
    randombytes_buf(bytes, sizeof(bytes));
    sodium_bin2hex(hex, sizeof(hex), bytes, sizeof(bytes));
    sodium_memzero(bytes, sizeof(bytes));
    return hex;
}
inline std::string Digest(const std::string &value) {
    Init();
    unsigned char digest[crypto_generichash_BYTES]; char hex[crypto_generichash_BYTES * 2 + 1];
    crypto_generichash(digest, sizeof(digest), reinterpret_cast<const unsigned char*>(value.data()), value.size(), nullptr, 0);
    sodium_bin2hex(hex, sizeof(hex), digest, sizeof(digest));
    return hex;
}
}
