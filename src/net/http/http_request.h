#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>

namespace minis3 {

/**
 * HTTP 方法枚举
 */
enum class HttpMethod {
    UNKNOWN = 0,
    GET,
    POST,
    PUT,
    DELETE,
    HEAD,
    OPTIONS,
    PATCH
};

/**
 * 将字符串转换为 HttpMethod
 */
HttpMethod StringToMethod(std::string_view method);

/**
 * 将 HttpMethod 转换为字符串
 */
const char* HttpMethodToString(HttpMethod method);

/**
 * HTTP 请求
 */
class HttpRequest {
public:
    HttpRequest() = default;
    
    // 请求行
    void SetMethod(HttpMethod method) { method_ = method; }
    HttpMethod Method() const { return method_; }
    
    void SetPath(std::string path) { path_ = std::move(path); }
    const std::string& Path() const { return path_; }
    
    void SetQuery(std::string query) { query_ = std::move(query); }
    const std::string& Query() const { return query_; }
    
    void SetVersion(std::string version) { version_ = std::move(version); }
    const std::string& Version() const { return version_; }
    
    // Headers
    void AddHeader(std::string key, std::string value);
    void SetHeader(const std::string& key, const std::string& value);
    std::string GetHeader(std::string_view key) const;
    const std::unordered_map<std::string, std::string>& Headers() const { return headers_; }
    
    // 常用 Headers 快捷方法
    size_t ContentLength() const;
    std::string_view ContentType() const;
    std::string_view Host() const;
    bool IsKeepAlive() const;
    
    // Body（对于小请求）
    void SetBody(std::string body) { body_ = std::move(body); }
    void AppendBody(std::string_view data) { body_.append(data); }
    const std::string& Body() const { return body_; }
    
    // 路径参数（由路由器填充）
    void SetPathParam(const std::string& key, const std::string& value) {
        path_params_[key] = value;
    }
    std::string GetPathParam(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& PathParams() const { return path_params_; }
    
    // 查询参数
    std::string GetQueryParam(const std::string& key) const;
    
    // 上下文信息
    void SetTraceId(std::string trace_id) { trace_id_ = std::move(trace_id); }
    const std::string& TraceId() const { return trace_id_; }
    
    void SetClientIp(std::string ip) { client_ip_ = std::move(ip); }
    const std::string& ClientIp() const { return client_ip_; }
    
    // 重置（用于连接复用）
    void Reset();

private:
    HttpMethod method_ = HttpMethod::UNKNOWN;
    std::string path_;
    std::string query_;
    std::string version_ = "HTTP/1.1";
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    
    std::unordered_map<std::string, std::string> path_params_;
    mutable std::unordered_map<std::string, std::string> query_params_;
    mutable bool query_parsed_ = false;
    
    std::string trace_id_;
    std::string client_ip_;
    
    void ParseQueryString() const;
};

} // namespace minis3
