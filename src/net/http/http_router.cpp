#include "http_router.h"
#include <spdlog/spdlog.h>

namespace minis3 {

std::pair<std::regex, std::vector<std::string>> HttpRouter::CompilePattern(const std::string& pattern) {
    // 将 /path/{param} 编译为正则并记录参数名
    std::vector<std::string> param_names;
    std::string regex_str = "^";
    
    size_t i = 0;
    while (i < pattern.size()) {
        if (pattern[i] == '{') {
            // 找到参数
            size_t end = pattern.find('}', i);
            if (end == std::string::npos) {
                throw std::invalid_argument("Invalid route pattern: unmatched '{'");
            }
            
            std::string param_name = pattern.substr(i + 1, end - i - 1);
            param_names.push_back(param_name);
            
            // 匹配非 / 的字符
            regex_str += "([^/]+)";
            i = end + 1;
        } else if (pattern[i] == '*') {
            // 通配符，匹配任意字符
            regex_str += "(.*)";
            ++i;
        } else {
            // 普通字符，需要转义特殊字符
            if (pattern[i] == '.' || pattern[i] == '+' || pattern[i] == '?' ||
                pattern[i] == '(' || pattern[i] == ')' || pattern[i] == '[' ||
                pattern[i] == ']' || pattern[i] == '$' || pattern[i] == '^') {
                regex_str += '\\';
            }
            regex_str += pattern[i];
            ++i;
        }
    }
    
    regex_str += "$";
    
    return {std::regex(regex_str), param_names};
}

void HttpRouter::Route(HttpMethod method, const std::string& pattern, HttpHandler handler) {
    // 注册路由：方法 + 模式 + 处理器
    RouteEntry route;
    route.method = method;
    route.pattern = pattern;
    route.handler = std::move(handler);
    
    auto [regex, param_names] = CompilePattern(pattern);
    route.regex = std::move(regex);
    route.param_names = std::move(param_names);
    
    routes_.push_back(std::move(route));
    
    spdlog::debug("Registered route: {} {}", HttpMethodToString(method), pattern);
}

void HttpRouter::Use(Middleware middleware) {
    // 注册全局中间件（按注册顺序执行）
    middlewares_.push_back(std::move(middleware));
}

HttpResponse HttpRouter::Handle(HttpRequest& request) {
    // 查找匹配的路由
    for (const auto& route : routes_) {
        if (route.method != request.Method()) {
            continue;
        }
        
        std::smatch match;
        std::string path = request.Path();
        
        if (std::regex_match(path, match, route.regex)) {
            // 提取路径参数
            for (size_t i = 0; i < route.param_names.size() && i + 1 < match.size(); ++i) {
                request.SetPathParam(route.param_names[i], match[i + 1].str());
            }
            
            // 构建中间件链（从后往前包装）
            HttpHandler final_handler = route.handler;
            
            // 从后往前包装中间件
            for (auto it = middlewares_.rbegin(); it != middlewares_.rend(); ++it) {
                auto& middleware = *it;
                final_handler = [&middleware, handler = std::move(final_handler)](HttpRequest& req) {
                    return middleware(req, handler);
                };
            }
            
            return final_handler(request);
        }
    }
    
    // 没有匹配的路由
    if (not_found_handler_) {
        return not_found_handler_(request);
    }
    
    return HttpResponse::NotFound("Route not found: " + request.Path(), request.TraceId());
}

} // namespace minis3
