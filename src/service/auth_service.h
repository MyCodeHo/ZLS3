#pragma once

#include "util/status.h"
#include <string>
#include <vector>

namespace minis3 {

/**
 * 认证服务
 */
class AuthService {
public:
    AuthService(const std::vector<std::string>& static_tokens);
    
    /**
     * 验证 Token
     */
    bool ValidateToken(const std::string& token) const;
    
    /**
     * 验证 API Key
     */
    bool ValidateApiKey(const std::string& key_id, const std::string& key_secret) const;
    
    /**
     * 验证预签名 URL
     */
    bool ValidatePresign(const std::string& method, const std::string& path,
                         int64_t expires, const std::string& signature) const;
    
    /**
     * 生成预签名 URL 签名
     */
    std::string GeneratePresignSignature(const std::string& method,
                                         const std::string& path,
                                         int64_t expires) const;

private:
    std::vector<std::string> static_tokens_;
};

} // namespace minis3
