#pragma once

#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include <vector>
#include <string>

namespace minis3 {

/**
 * 预签名 URL 相关的 HTTP 处理函数
 */
class PresignHandlers {
public:
    /**
     * POST /presign
     * 创建预签名 URL
     */
    static HttpResponse CreatePresignUrl(HttpRequest& request, 
                                          const std::vector<std::string>& secrets);
    
    /**
     * 验证预签名 URL 的签名
     * 在 AuthMiddleware 中调用
     */
    static bool VerifyPresignUrl(const HttpRequest& request,
                                 const std::vector<std::string>& secrets);
    
    /**
     * 生成签名
     */
    static std::string GenerateSignature(const std::string& secret,
                                         const std::string& method,
                                         const std::string& path,
                                         int64_t expires);
};

} // namespace minis3
