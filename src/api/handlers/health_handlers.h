#pragma once

#include "net/http/http_request.h"
#include "net/http/http_response.h"

namespace minis3 {

/**
 * 健康检查相关的 HTTP 处理函数
 */
class HealthHandlers {
public:
    /**
     * GET /healthz
     * 健康检查
     */
    static HttpResponse HealthCheck(HttpRequest& request);
    
    /**
     * GET /readyz
     * 就绪检查
     */
    static HttpResponse ReadyCheck(HttpRequest& request);
    
    /**
     * GET /metrics
     * Prometheus 指标
     */
    static HttpResponse Metrics(HttpRequest& request);
};

} // namespace minis3
