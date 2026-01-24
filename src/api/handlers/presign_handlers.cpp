#include "presign_handlers.h"
#include "util/crypto.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>

namespace minis3 {

using json = nlohmann::json;

HttpResponse PresignHandlers::CreatePresignUrl(HttpRequest& request,
                                                const std::vector<std::string>& secrets) {
    // 解析请求体
    json request_body;
    try {
        request_body = json::parse(request.Body());
    } catch (...) {
        return HttpResponse::BadRequest("Invalid JSON body");
    }
    
    // 验证必需字段
    if (!request_body.contains("bucket") || !request_body.contains("object")) {
        return HttpResponse::BadRequest("Missing bucket or object");
    }
    
    std::string bucket = request_body["bucket"].get<std::string>();
    std::string object = request_body["object"].get<std::string>();
    std::string method = "GET";
    int ttl_seconds = 600;  // 默认 10 分钟
    
    // 解析 method（当前仅支持 GET）
    if (request_body.contains("method")) {
        method = request_body["method"].get<std::string>();
        // 目前只支持 GET
        if (method != "GET") {
            return HttpResponse::BadRequest("Only GET method is supported for presigned URLs");
        }
    }
    
    // 解析过期时间（最长 7 天）
    if (request_body.contains("ttl_seconds")) {
        ttl_seconds = request_body["ttl_seconds"].get<int>();
        // 限制最长 7 天
        if (ttl_seconds < 1 || ttl_seconds > 7 * 24 * 3600) {
            return HttpResponse::BadRequest("TTL must be between 1 and 604800 seconds");
        }
    }
    
    // 计算过期时间
    auto now = std::chrono::system_clock::now();
    auto expires_at = now + std::chrono::seconds(ttl_seconds);
    int64_t expires_timestamp = std::chrono::system_clock::to_time_t(expires_at);
    
    // 构建路径
    std::string path = "/buckets/" + bucket + "/objects/" + object;
    
    // 使用第一个 secret 生成签名
    if (secrets.empty()) {
        return HttpResponse::InternalError("No signing key configured");
    }
    
    std::string signature = GenerateSignature(secrets[0], method, path, expires_timestamp);
    
    // 构建预签名 URL
    std::string presigned_url = path + "?expires=" + std::to_string(expires_timestamp) +
                                "&sig=" + signature + "&method=" + method;
    
    // 构建响应
    json response_body = {
        {"url", presigned_url},
        {"expires_at", expires_timestamp},
        {"bucket", bucket},
        {"object", object},
        {"method", method}
    };
    
    return HttpResponse::OK(response_body.dump());
}

bool PresignHandlers::VerifyPresignUrl(const HttpRequest& request,
                                       const std::vector<std::string>& secrets) {
    // 获取查询参数
    std::string expires_str = request.GetQueryParam("expires");
    std::string sig = request.GetQueryParam("sig");
    std::string method = request.GetQueryParam("method");
    
    if (expires_str.empty() || sig.empty()) {
        return false;
    }
    
    if (method.empty()) {
        method = "GET";
    }
    
    // 检查过期时间
    int64_t expires;
    try {
        expires = std::stoll(expires_str);
    } catch (...) {
        return false;
    }
    
    auto now = std::chrono::system_clock::now();
    int64_t now_timestamp = std::chrono::system_clock::to_time_t(now);
    
    if (now_timestamp > expires) {
        spdlog::debug("Presigned URL expired: {} > {}", now_timestamp, expires);
        return false;
    }
    
    // 验证方法是否匹配
    if (HttpMethodToString(request.Method()) != method) {
        spdlog::debug("Method mismatch: {} != {}", HttpMethodToString(request.Method()), method);
        return false;
    }
    
    // 获取路径（不含查询参数）
    std::string path = request.Path();
    
    // 尝试所有 secrets 验证签名
    for (const auto& secret : secrets) {
        std::string expected_sig = GenerateSignature(secret, method, path, expires);
        if (expected_sig == sig) {
            return true;
        }
    }
    
    spdlog::debug("Signature verification failed for path: {}", path);
    return false;
}

std::string PresignHandlers::GenerateSignature(const std::string& secret,
                                               const std::string& method,
                                               const std::string& path,
                                               int64_t expires) {
    // 构建签名字符串
    std::string string_to_sign = method + "\n" + path + "\n" + std::to_string(expires);
    
    // 使用 HMAC-SHA256 生成签名
    return Crypto::HMAC_SHA256(secret, string_to_sign);
}

} // namespace minis3
