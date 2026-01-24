#include "presign_service.h"
#include <chrono>
#include <sstream>

namespace minis3 {

PresignService::PresignService(const AuthService& auth_service, const std::string& base_url)
    : auth_service_(auth_service), base_url_(base_url) {
}

Status PresignService::GenerateGetUrl(const std::string& bucket, const std::string& key,
                                       int64_t expires_seconds, std::string& url) {
    // 构建路径
    std::string path = "/" + bucket + "/" + key;
    
    // 计算过期时间戳
    auto now = std::chrono::system_clock::now();
    int64_t expires = std::chrono::system_clock::to_time_t(now) + expires_seconds;
    
    // 生成签名
    std::string signature = auth_service_.GeneratePresignSignature("GET", path, expires);
    
    // 构建 URL
    url = BuildPresignUrl("GET", path, expires, signature);
    
    return Status::OK();
}

Status PresignService::GeneratePutUrl(const std::string& bucket, const std::string& key,
                                       int64_t expires_seconds, std::string& url) {
    // 构建路径
    std::string path = "/" + bucket + "/" + key;
    
    // 计算过期时间戳
    auto now = std::chrono::system_clock::now();
    int64_t expires = std::chrono::system_clock::to_time_t(now) + expires_seconds;
    
    // 生成签名
    std::string signature = auth_service_.GeneratePresignSignature("PUT", path, expires);
    
    // 构建 URL
    url = BuildPresignUrl("PUT", path, expires, signature);
    
    return Status::OK();
}

Status PresignService::ValidatePresignUrl(const std::string& method, const std::string& path,
                                           const std::string& expires_str, const std::string& signature) {
    // 解析过期时间
    int64_t expires = 0;
    try {
        expires = std::stoll(expires_str);
    } catch (...) {
        return Status::InvalidArgument("Invalid expires parameter");
    }
    
    // 验证签名
    if (!auth_service_.ValidatePresign(method, path, expires, signature)) {
        return Status::Unauthorized("Invalid or expired signature");
    }
    
    return Status::OK();
}

std::string PresignService::BuildPresignUrl(const std::string& method, const std::string& path,
                                             int64_t expires, const std::string& signature) {
    // 组装最终 URL
    std::ostringstream oss;
    oss << base_url_ << path;
    oss << "?X-Method=" << method;
    oss << "&X-Expires=" << expires;
    oss << "&X-Signature=" << signature;
    return oss.str();
}

} // namespace minis3
