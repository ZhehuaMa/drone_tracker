#ifndef PLATFORM_FILE_IDENTITY_H
#define PLATFORM_FILE_IDENTITY_H

#include <array>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include <openssl/evp.h>

/** @brief 初始化阶段计算文件 SHA-256；禁止在逐帧路径调用。 */
inline std::string recordingFileIdentity(const std::string &path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) { throw std::runtime_error("Cannot fingerprint recording identity: " + path); }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("Recording SHA-256 initialization failed");
    }
    std::array<char, 65536> buffer{};
    while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
        if (EVP_DigestUpdate(context.get(), buffer.data(), input.gcount()) != 1) {
            throw std::runtime_error("Recording SHA-256 update failed");
        }
    }
    if (input.bad()) { throw std::runtime_error("Recording identity read failed: " + path); }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned size{};
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &size) != 1) { throw std::runtime_error("SHA-256 failed"); }
    std::ostringstream result{};
    result << std::hex << std::setfill('0');
    for (unsigned i{}; i < size; ++i) { result << std::setw(2) << unsigned{digest[i]}; }
    return result.str();
}
#endif // PLATFORM_FILE_IDENTITY_H
