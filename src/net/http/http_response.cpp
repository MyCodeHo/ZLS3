#include "http_response.h"
#include "net/buffer/byte_buffer.h"
#include <nlohmann/json.hpp>
#include <sstream>

namespace minis3 {

void HttpResponse::SetHeader(const std::string& key, const std::string& value) {
    // 覆盖设置 header
    headers_[key] = value;
}

void HttpResponse::AddHeader(const std::string& key, const std::string& value) {
    // 追加 header（支持多值）
    auto it = headers_.find(key);
    if (it != headers_.end()) {
        it->second += ", " + value;
    } else {
        headers_[key] = value;
    }
}

void HttpResponse::SetJsonBody(const std::string& json) {
    // 设置 JSON body 并补充 Content-Type
    body_ = json;
    SetHeader("Content-Type", "application/json; charset=utf-8");
}

void HttpResponse::SetFile(const std::string& file_path, size_t file_size,
                           size_t offset, size_t length) {
    // 配置文件响应（用于 sendfile）
    file_path_ = file_path;
    file_size_ = file_size;
    file_offset_ = offset;
    file_length_ = (length > 0) ? length : file_size;
}

void HttpResponse::SetFileRange(size_t offset, size_t length) {
    file_offset_ = offset;
    file_length_ = length;
}

void HttpResponse::AppendToBuffer(ByteBuffer* buffer) const {
    // 序列化响应行与 headers（body 由连接层处理）
    std::ostringstream oss;
    
    // 状态行
    oss << "HTTP/1.1 " << status_code_ << " " << status_message_ << "\r\n";
    
    // Headers
    for (const auto& [key, value] : headers_) {
        oss << key << ": " << value << "\r\n";
    }
    
    // Content-Length（如果有 body 且没有设置）
    if (!body_.empty() && headers_.find("Content-Length") == headers_.end()) {
        oss << "Content-Length: " << body_.size() << "\r\n";
    }
    
    // Connection
    if (headers_.find("Connection") == headers_.end()) {
        oss << "Connection: " << (keep_alive_ ? "keep-alive" : "close") << "\r\n";
    }
    
    // 空行
    oss << "\r\n";
    
    // Body（如果不是文件响应）
    if (!HasFile() && !body_.empty()) {
        oss << body_;
    }
    
    buffer->Append(oss.str());
}

void HttpResponse::Reset() {
    // 重置响应对象
    status_code_ = 200;
    status_message_ = "OK";
    headers_.clear();
    body_.clear();
    file_path_.clear();
    file_size_ = 0;
    file_offset_ = 0;
    file_length_ = 0;
    keep_alive_ = true;
}

std::string HttpResponse::GetStatusMessage(int code) {
    // 将状态码映射为默认消息
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 416: return "Range Not Satisfiable";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

HttpResponse HttpResponse::Ok(const std::string& body) {
    // 200 OK（可选 body）
    HttpResponse resp;
    resp.SetStatusCode(200);
    resp.SetStatusMessage("OK");
    if (!body.empty()) {
        resp.SetBody(body);
    }
    return resp;
}

HttpResponse HttpResponse::Json(const std::string& json, int status_code) {
    // JSON 响应
    HttpResponse resp;
    resp.SetStatusCode(status_code);
    resp.SetStatusMessage(GetStatusMessage(status_code));
    resp.SetJsonBody(json);
    return resp;
}

HttpResponse HttpResponse::Error(int status_code, const std::string& code,
                                 const std::string& message, const std::string& trace_id) {
    // 标准错误响应结构
    nlohmann::json error = {
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };
    
    if (!trace_id.empty()) {
        error["error"]["trace_id"] = trace_id;
    }
    
    HttpResponse resp;
    resp.SetStatusCode(status_code);
    resp.SetStatusMessage(GetStatusMessage(status_code));
    resp.SetJsonBody(error.dump());
    return resp;
}

HttpResponse HttpResponse::NotFound(const std::string& message, const std::string& trace_id) {
    return Error(404, "NotFound", message, trace_id);
}

HttpResponse HttpResponse::BadRequest(const std::string& message, const std::string& trace_id) {
    return Error(400, "BadRequest", message, trace_id);
}

HttpResponse HttpResponse::Unauthorized(const std::string& message, const std::string& trace_id) {
    return Error(401, "Unauthorized", message, trace_id);
}

HttpResponse HttpResponse::Forbidden(const std::string& message, const std::string& trace_id) {
    return Error(403, "Forbidden", message, trace_id);
}

HttpResponse HttpResponse::Conflict(const std::string& message, const std::string& trace_id) {
    return Error(409, "Conflict", message, trace_id);
}

HttpResponse HttpResponse::InternalError(const std::string& message, const std::string& trace_id) {
    return Error(500, "InternalError", message, trace_id);
}

HttpResponse HttpResponse::ServiceUnavailable(const std::string& message, const std::string& trace_id) {
    return Error(503, "ServiceUnavailable", message, trace_id);
}

HttpResponse HttpResponse::RangeNotSatisfiable(size_t file_size) {
    // Range 不满足时返回 416
    HttpResponse resp;
    resp.SetStatusCode(416);
    resp.SetStatusMessage("Range Not Satisfiable");
    resp.SetHeader("Content-Range", "bytes */" + std::to_string(file_size));
    return resp;
}

HttpResponse HttpResponse::OK(const std::string& body) {
    return Ok(body);
}

HttpResponse HttpResponse::Created(const std::string& body) {
    HttpResponse resp;
    resp.SetStatusCode(201);
    resp.SetStatusMessage("Created");
    if (!body.empty()) {
        resp.SetJsonBody(body);
    }
    return resp;
}

HttpResponse HttpResponse::NoContent() {
    HttpResponse resp;
    resp.SetStatusCode(204);
    resp.SetStatusMessage("No Content");
    return resp;
}

HttpResponse::HttpResponse(HttpStatusCode code) {
    // 通过枚举构造响应
    SetStatusCode(code);
    SetStatusMessage(GetStatusMessage(static_cast<int>(code)));
}

} // namespace minis3
