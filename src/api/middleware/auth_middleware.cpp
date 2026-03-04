#include "auth_middleware.h"
#include "api/handlers/presign_handlers.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace minis3 {

HttpResponse AuthMiddleware::Handle(HttpRequest& request,
                                    HttpHandler next,
                                    const std::vector<std::string>& valid_tokens) {
    // 检查是否需要认证
    if (!RequiresAuth(request.Path())) {
        return next(request);
    }
    
    // 检查预签名 URL
    if (!request.GetQueryParam("sig").empty()) {
        if (PresignHandlers::VerifyPresignUrl(request, valid_tokens)) {
            request.SetHeader("X-Auth-Method", "presign");
            return next(request);
        }
        return HttpResponse::Forbidden("Invalid or expired signature");
    }
    
    // 检查 Authorization 头（Bearer Token）
    std::string auth_header = request.GetHeader("Authorization");
    if (!auth_header.empty()) {
        if (ValidateBearerToken(auth_header, valid_tokens)) {
            request.SetHeader("X-Auth-Method", "bearer");
            return next(request);
        }
    }
    
    // 检查 X-API-Key 头
    std::string api_key = request.GetHeader("X-API-Key");
    if (!api_key.empty()) {
        if (ValidateApiKey(api_key, valid_tokens)) {
            request.SetHeader("X-Auth-Method", "apikey");
            return next(request);
        }
    }
    
    spdlog::debug("Authentication failed for path: {}", request.Path());
    return HttpResponse::Unauthorized("Authentication required");
}

bool AuthMiddleware::RequiresAuth(const std::string& path) {
    // 不需要认证的路径
    static const std::vector<std::string> public_paths = {
        "/healthz",
        "/readyz",
        "/metrics"
    };
    
    for (const auto& public_path : public_paths) {
        if (path == public_path) {
            return false;
        }
    }
    
    return true;
}

bool AuthMiddleware::ValidateBearerToken(const std::string& auth_header,
                                         const std::vector<std::string>& valid_tokens) {
    // 格式: Bearer <token>
    const std::string prefix = "Bearer ";
    if (auth_header.length() <= prefix.length()) {
        return false;
    }
    
    if (auth_header.substr(0, prefix.length()) != prefix) {
        return false;
    }
    
    std::string token = auth_header.substr(prefix.length());
    
    return std::find(valid_tokens.begin(), valid_tokens.end(), token) != valid_tokens.end();
}

bool AuthMiddleware::ValidateApiKey(const std::string& api_key,
                                    const std::vector<std::string>& valid_tokens) {
    // 简单白名单匹配
    return std::find(valid_tokens.begin(), valid_tokens.end(), api_key) != valid_tokens.end();
}

} // namespace minis3
