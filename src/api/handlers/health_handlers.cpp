#include "health_handlers.h"
#include "util/metrics.h"
#include <nlohmann/json.hpp>

namespace minis3 {

using json = nlohmann::json;

HttpResponse HealthHandlers::HealthCheck(HttpRequest& request) {
    // 基础健康检查
    (void)request;  // unused
    
    json response_body = {
        {"status", "healthy"},
        {"service", "minis3"},
        {"version", "1.0.0"}
    };
    
    return HttpResponse::OK(response_body.dump());
}

HttpResponse HealthHandlers::ReadyCheck(HttpRequest& request) {
    // 依赖就绪检查（目前为占位）
    (void)request;  // unused
    
    // TODO: 检查 MySQL 连接、存储目录等
    bool is_ready = true;
    
    if (is_ready) {
        json response_body = {
            {"status", "ready"}
        };
        return HttpResponse::OK(response_body.dump());
    } else {
        json response_body = {
            {"status", "not ready"},
            {"reason", "dependencies not available"}
        };
        return HttpResponse::ServiceUnavailable(response_body.dump());
    }
}

HttpResponse HealthHandlers::Metrics(HttpRequest& request) {
    // 导出 Prometheus 指标
    (void)request;  // unused
    
    // 返回 Prometheus 格式的指标
    std::string metrics = Metrics::GetInstance().Export();
    
    HttpResponse response(HttpStatusCode::OK);
    response.SetHeader("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
    response.SetBody(metrics);
    
    return response;
}

} // namespace minis3
