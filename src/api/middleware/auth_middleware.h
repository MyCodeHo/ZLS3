#pragma once

#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_router.h"
#include <vector>
#include <string>

namespace minis3 {

/**
 * 认证中间件
 */
class AuthMiddleware {
public:
    /**
     * 处理认证
     * @param request HTTP 请求
     * @param next 下一个处理函数
     * @param valid_tokens 有效的 token 列表
     */
    static HttpResponse Handle(HttpRequest& request, 
                               HttpHandler next,
                               const std::vector<std::string>& valid_tokens);
    
private:
    /**
     * 检查路径是否需要认证
     */
    static bool RequiresAuth(const std::string& path);
    
    /**
     * 验证 Bearer Token
     */
    static bool ValidateBearerToken(const std::string& auth_header,
                                    const std::vector<std::string>& valid_tokens);
    
    /**
     * 验证 API Key
     */
    static bool ValidateApiKey(const std::string& api_key,
                               const std::vector<std::string>& valid_tokens);
};

} // namespace minis3
