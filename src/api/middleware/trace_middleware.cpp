#include "trace_middleware.h"
#include "util/uuid.h"

namespace minis3 {

HttpResponse TraceMiddleware::Handle(HttpRequest& request, HttpHandler next) {
    // 检查是否已有 X-Request-Id
    std::string trace_id = request.GetHeader("X-Request-Id");
    
    if (trace_id.empty()) {
        // 生成新的 trace_id
        trace_id = UUID::Generate();
        request.SetHeader("X-Request-Id", trace_id);
    }
    
    // 存储 trace_id 供后续使用
    request.SetTraceId(trace_id);
    
    // 调用下一个处理函数
    HttpResponse response = next(request);
    
    // 在响应中也添加 trace_id
    response.SetHeader("X-Request-Id", trace_id);
    
    return response;
}

} // namespace minis3
