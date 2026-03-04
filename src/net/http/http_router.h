#pragma once

#include "http_request.h"
#include "http_response.h"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <regex>

namespace minis3 {

/**
 * HTTP 路由处理函数类型
 */
using HttpHandler = std::function<HttpResponse(HttpRequest&)>;

/**
 * 中间件类型
 */
using Middleware = std::function<HttpResponse(HttpRequest&, HttpHandler)>;

/**
 * HTTP 路由器
 * 
 * 支持路径参数，例如 /buckets/{bucket}/objects/{object}
 */
class HttpRouter {
public:
    HttpRouter() = default;
    
    /**
     * 注册路由
     * @param method HTTP 方法
     * @param pattern 路径模式，支持 {param} 格式的参数
     * @param handler 处理函数
     */
    void Route(HttpMethod method, const std::string& pattern, HttpHandler handler);
    
    // 便捷方法
    void Get(const std::string& pattern, HttpHandler handler) {
        Route(HttpMethod::GET, pattern, std::move(handler));
    }
    
    void Post(const std::string& pattern, HttpHandler handler) {
        Route(HttpMethod::POST, pattern, std::move(handler));
    }
    
    void Put(const std::string& pattern, HttpHandler handler) {
        Route(HttpMethod::PUT, pattern, std::move(handler));
    }
    
    void Delete(const std::string& pattern, HttpHandler handler) {
        Route(HttpMethod::DELETE, pattern, std::move(handler));
    }
    
    void Head(const std::string& pattern, HttpHandler handler) {
        Route(HttpMethod::HEAD, pattern, std::move(handler));
    }
    
    /**
     * 添加全局中间件
     */
    void Use(Middleware middleware);
    
    /**
     * 匹配路由并处理请求
     */
    HttpResponse Handle(HttpRequest& request);
    
    /**
     * 设置 404 处理函数
     */
    void SetNotFoundHandler(HttpHandler handler) {
        not_found_handler_ = std::move(handler);
    }

private:
    struct RouteEntry {
        HttpMethod method;
        std::string pattern;
        std::regex regex;
        std::vector<std::string> param_names;
        HttpHandler handler;
    };
    
    static std::pair<std::regex, std::vector<std::string>> CompilePattern(const std::string& pattern);
    
    std::vector<RouteEntry> routes_;
    std::vector<Middleware> middlewares_;
    HttpHandler not_found_handler_;
};

} // namespace minis3
