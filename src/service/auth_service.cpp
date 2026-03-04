#include "auth_service.h"
#include "util/crypto.h"
#include <algorithm>
#include <chrono>

namespace minis3 {

AuthService::AuthService(const std::vector<std::string>& static_tokens)
    : static_tokens_(static_tokens) {
}

bool AuthService::ValidateToken(const std::string& token) const {
    // 静态 token 白名单匹配
    return std::find(static_tokens_.begin(), static_tokens_.end(), token) 
           != static_tokens_.end();
}

bool AuthService::ValidateApiKey(const std::string& key_id, const std::string& key_secret) const {
    // 简化实现：将 key_id:key_secret 组合作为 token
    std::string combined = key_id + ":" + key_secret;
    return ValidateToken(combined);
}

bool AuthService::ValidatePresign(const std::string& method, const std::string& path,
                                  int64_t expires, const std::string& signature) const {
    // 检查是否过期
    auto now = std::chrono::system_clock::now();
    auto now_ts = std::chrono::system_clock::to_time_t(now);
    
    if (now_ts > expires) {
        return false;
    }
    
    // 验证签名
    std::string expected = GeneratePresignSignature(method, path, expires);
    return expected == signature;
}

std::string AuthService::GeneratePresignSignature(const std::string& method,
                                                   const std::string& path,
                                                   int64_t expires) const {
    if (static_tokens_.empty()) {
        return "";
    }
    
    // 使用第一个 token 作为签名密钥
    std::string string_to_sign = method + "\n" + path + "\n" + std::to_string(expires);
    return Crypto::HMAC_SHA256(static_tokens_[0], string_to_sign);
}

} // namespace minis3
