#include "crypto.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace minis3 {

std::string Crypto::ToHexString(const uint8_t* data, size_t len) {
    // 将二进制数据转为十六进制字符串
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

std::string Crypto::SHA256(const std::string& data) {
    return SHA256(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string Crypto::SHA256(const uint8_t* data, size_t len) {
    // 使用 OpenSSL 计算 SHA256
    uint8_t hash[SHA256_DIGEST_LENGTH];
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }
    
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data, len) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA256 computation failed");
    }
    
    EVP_MD_CTX_free(ctx);
    return ToHexString(hash, SHA256_DIGEST_LENGTH);
}

std::string Crypto::SHA256File(const std::string& filepath) {
    // 流式读取文件并计算 SHA256
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }
    
    SHA256Context ctx;
    
    char buffer[65536];  // 64KB buffer
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        ctx.Update(reinterpret_cast<uint8_t*>(buffer), file.gcount());
    }
    
    return ctx.Final();
}

std::string Crypto::HMAC_SHA256(const std::string& key, const std::string& data) {
    // 计算 HMAC-SHA256
    uint8_t hash[SHA256_DIGEST_LENGTH];
    unsigned int hash_len = 0;
    
    HMAC(EVP_sha256(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const uint8_t*>(data.data()), data.size(),
         hash, &hash_len);
    
    return ToHexString(hash, hash_len);
}

// SHA256Context 实现

Crypto::SHA256Context::SHA256Context() {
    // 初始化上下文
    ctx_ = EVP_MD_CTX_new();
    if (!ctx_) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }
    Reset();
}

Crypto::SHA256Context::~SHA256Context() {
    if (ctx_) {
        EVP_MD_CTX_free(static_cast<EVP_MD_CTX*>(ctx_));
    }
}

Crypto::SHA256Context::SHA256Context(SHA256Context&& other) noexcept 
    : ctx_(other.ctx_) {
    other.ctx_ = nullptr;
}

Crypto::SHA256Context& Crypto::SHA256Context::operator=(SHA256Context&& other) noexcept {
    if (this != &other) {
        if (ctx_) {
            EVP_MD_CTX_free(static_cast<EVP_MD_CTX*>(ctx_));
        }
        ctx_ = other.ctx_;
        other.ctx_ = nullptr;
    }
    return *this;
}

void Crypto::SHA256Context::Update(const uint8_t* data, size_t len) {
    // 增量更新哈希
    if (EVP_DigestUpdate(static_cast<EVP_MD_CTX*>(ctx_), data, len) != 1) {
        throw std::runtime_error("SHA256 update failed");
    }
}

void Crypto::SHA256Context::Update(const std::string& data) {
    Update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string Crypto::SHA256Context::Final() {
    // 计算最终哈希并返回十六进制
    uint8_t hash[SHA256_DIGEST_LENGTH];
    
    if (EVP_DigestFinal_ex(static_cast<EVP_MD_CTX*>(ctx_), hash, nullptr) != 1) {
        throw std::runtime_error("SHA256 final failed");
    }
    
    return ToHexString(hash, SHA256_DIGEST_LENGTH);
}

void Crypto::SHA256Context::Reset() {
    // 重置上下文以便复用
    if (EVP_DigestInit_ex(static_cast<EVP_MD_CTX*>(ctx_), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("SHA256 init failed");
    }
}

} // namespace minis3
