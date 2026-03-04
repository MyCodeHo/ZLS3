#pragma once

#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "net/http/http_router.h"

namespace minis3 {

/**
 * Trace 中间件
 * 
 * 负责生成和注入 trace_id
 */
class TraceMiddleware {
public:
    /**
     * 处理请求
     */
    static HttpResponse Handle(HttpRequest& request, HttpHandler next);
};

} // namespace minis3
