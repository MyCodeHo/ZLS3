#include "http_request.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace minis3 {

HttpMethod StringToMethod(std::string_view method) {
    // 将字符串方法名映射为枚举
    if (method == "GET") return HttpMethod::GET;
    if (method == "POST") return HttpMethod::POST;
    if (method == "PUT") return HttpMethod::PUT;
    if (method == "DELETE") return HttpMethod::DELETE;
    if (method == "HEAD") return HttpMethod::HEAD;
    if (method == "OPTIONS") return HttpMethod::OPTIONS;
    if (method == "PATCH") return HttpMethod::PATCH;
    return HttpMethod::UNKNOWN;
}

std::string_view MethodToString(HttpMethod method) {
    // 将枚举转换为字符串（视图）
    switch (method) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        case HttpMethod::HEAD: return "HEAD";
        case HttpMethod::OPTIONS: return "OPTIONS";
        case HttpMethod::PATCH: return "PATCH";
        default: return "UNKNOWN";
    }
}

const char* HttpMethodToString(HttpMethod method) {
    // 将枚举转换为 C 字符串
    switch (method) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        case HttpMethod::HEAD: return "HEAD";
        case HttpMethod::OPTIONS: return "OPTIONS";
        case HttpMethod::PATCH: return "PATCH";
        default: return "UNKNOWN";
    }
}

void HttpRequest::AddHeader(std::string key, std::string value) {
    // 转换为小写（HTTP headers 不区分大小写）
    std::transform(key.begin(), key.end(), key.begin(), 
        [](unsigned char c) { return std::tolower(c); });
    headers_[std::move(key)] = std::move(value);
}

void HttpRequest::SetHeader(const std::string& key, const std::string& value) {
    // 统一存储为小写 key
    std::string lower_key = key;
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
        [](unsigned char c) { return std::tolower(c); });
    headers_[lower_key] = value;
}

std::string HttpRequest::GetHeader(std::string_view key) const {
    // 以小写查找 header
    std::string lower_key(key);
    std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
        [](unsigned char c) { return std::tolower(c); });
    
    auto it = headers_.find(lower_key);
    if (it != headers_.end()) {
        return it->second;
    }
    return "";
}

size_t HttpRequest::ContentLength() const {
    // 读取 Content-Length 并转换为数字
    std::string header = GetHeader("content-length");
    if (!header.empty()) {
        try {
            return std::stoull(header);
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

std::string_view HttpRequest::ContentType() const {
    // 返回 Content-Type（线程局部缓存）
    static thread_local std::string result;
    result = GetHeader("content-type");
    return result;
}

std::string_view HttpRequest::Host() const {
    // 返回 Host（线程局部缓存）
    static thread_local std::string result;
    result = GetHeader("host");
    return result;
}

bool HttpRequest::IsKeepAlive() const {
    // 根据 Connection header 判断是否 keep-alive
    std::string connection = GetHeader("connection");
    if (!connection.empty()) {
        std::transform(connection.begin(), connection.end(), connection.begin(),
            [](unsigned char c) { return std::tolower(c); });
        
        if (connection == "close") {
            return false;
        }
        if (connection == "keep-alive") {
            return true;
        }
    }
    
    // HTTP/1.1 默认 keep-alive
    return version_ == "HTTP/1.1";
}

std::string HttpRequest::GetPathParam(const std::string& key) const {
    auto it = path_params_.find(key);
    if (it != path_params_.end()) {
        return it->second;
    }
    return "";
}

void HttpRequest::ParseQueryString() const {
    // 懒解析 query string
    if (query_parsed_ || query_.empty()) {
        query_parsed_ = true;
        return;
    }
    
    std::istringstream stream(query_);
    std::string pair;
    
    while (std::getline(stream, pair, '&')) {
        auto eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = pair.substr(0, eq_pos);
            std::string value = pair.substr(eq_pos + 1);
            query_params_[key] = value;
        } else {
            query_params_[pair] = "";
        }
    }
    
    query_parsed_ = true;
}

std::string HttpRequest::GetQueryParam(const std::string& key) const {
    // 获取 query 参数，若未解析则先解析
    ParseQueryString();
    
    auto it = query_params_.find(key);
    if (it != query_params_.end()) {
        return it->second;
    }
    return "";
}

void HttpRequest::Reset() {
    // 重置请求（用于 keep-alive 复用）
    method_ = HttpMethod::UNKNOWN;
    path_.clear();
    query_.clear();
    version_ = "HTTP/1.1";
    headers_.clear();
    body_.clear();
    path_params_.clear();
    query_params_.clear();
    query_parsed_ = false;
    // 保留 trace_id 和 client_ip
}

} // namespace minis3
