#pragma once

#include "service/auth_service.h"
#include "util/status.h"
#include <string>
#include <cstdint>

namespace minis3 {

/**
 * 预签名 URL 服务层
 */
class PresignService {
public:
    explicit PresignService(const AuthService& auth_service, const std::string& base_url);
    
    /**
     * 生成预签名 GET URL
     */
    Status GenerateGetUrl(const std::string& bucket, const std::string& key,
                          int64_t expires_seconds, std::string& url);
    
    /**
     * 生成预签名 PUT URL
     */
    Status GeneratePutUrl(const std::string& bucket, const std::string& key,
                          int64_t expires_seconds, std::string& url);
    
    /**
     * 验证预签名 URL
     */
    Status ValidatePresignUrl(const std::string& method, const std::string& path,
                               const std::string& expires, const std::string& signature);

private:
    const AuthService& auth_service_;
    std::string base_url_;
    
    std::string BuildPresignUrl(const std::string& method, const std::string& path,
                                 int64_t expires, const std::string& signature);
};

} // namespace minis3
