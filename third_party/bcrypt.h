// bcrypt.h — Thin C++ wrapper around Linux crypt_r() for bcrypt password hashing.
// Uses the OS-provided bcrypt implementation ($2b$ format).
// Link with -lcrypt (standard on Linux, no extra dependency).
//
// API:
//   std::string bcrypt::generateHash(const std::string& password, unsigned workload = 12)
//   bool        bcrypt::validatePassword(const std::string& password, const std::string& hash)

#pragma once

#include <string>
#include <random>
#include <crypt.h>

namespace bcrypt {

namespace {

const char kBase64[] =
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

std::string generateSalt(unsigned workload) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 63);

    char salt[30];
    salt[0] = '$';
    salt[1] = '2';
    salt[2] = 'b';
    salt[3] = '$';
    salt[4] = '0' + (workload / 10);
    salt[5] = '0' + (workload % 10);
    salt[6] = '$';
    for (int i = 7; i < 29; i++) {
        salt[i] = kBase64[dist(gen)];
    }
    salt[29] = '\0';
    return std::string(salt);
}

} // anonymous namespace

inline std::string generateHash(const std::string& password, unsigned workload = 12) {
    std::string salt = generateSalt(workload);

    struct crypt_data data;
    data.initialized = 0;
    char* result = crypt_r(password.c_str(), salt.c_str(), &data);

    return std::string(result ? result : "");
}

inline bool validatePassword(const std::string& password, const std::string& hash) {
    struct crypt_data data;
    data.initialized = 0;
    char* result = crypt_r(password.c_str(), hash.c_str(), &data);

    return result && hash == result;
}

} // namespace bcrypt
