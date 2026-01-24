#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <functional>
#include <vector>

namespace minis3 {

class ByteBuffer;

/**
 * HTTP 状态码
 */
enum class HttpStatusCode {
    OK = 200,
    CREATED = 201,
    NO_CONTENT = 204,
    PARTIAL_CONTENT = 206,
    MOVED_PERMANENTLY = 301,
    FOUND = 302,
    NOT_MODIFIED = 304,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    METHOD_NOT_ALLOWED = 405,
    CONFLICT = 409,
    PAYLOAD_TOO_LARGE = 413,
    RANGE_NOT_SATISFIABLE = 416,
    INTERNAL_SERVER_ERROR = 500,
    SERVICE_UNAVAILABLE = 503
};

/**
 * HTTP 响应
 */
class HttpResponse {
public:
    HttpResponse() = default;
    explicit HttpResponse(HttpStatusCode code);
    
    // 状态码
    void SetStatusCode(int code) { status_code_ = code; }
    void SetStatusCode(HttpStatusCode code) { status_code_ = static_cast<int>(code); }
    int StatusCode() const { return status_code_; }
    
    void SetStatusMessage(std::string message) { status_message_ = std::move(message); }
    const std::string& StatusMessage() const { return status_message_; }
    
    // Headers
    void SetHeader(const std::string& key, const std::string& value);
    void AddHeader(const std::string& key, const std::string& value);
    const std::unordered_map<std::string, std::string>& Headers() const { return headers_; }
    
    // Body
    void SetBody(std::string body) { body_ = std::move(body); }
    void AppendBody(std::string_view data) { body_.append(data); }
    const std::string& Body() const { return body_; }
    
    // JSON 响应
    void SetJsonBody(const std::string& json);
    
    // 文件响应
    void SetFile(const std::string& file_path, size_t file_size,
                 size_t offset = 0, size_t length = 0);
    bool HasFile() const { return !file_path_.empty(); }
    const std::string& FilePath() const { return file_path_; }
    size_t FileSize() const { return file_size_; }
    size_t FileOffset() const { return file_offset_; }
    size_t FileLength() const { return file_length_ > 0 ? file_length_ : file_size_; }
    void SetFileOffset(size_t offset) { file_offset_ = offset; }
    void SetFileRange(size_t offset, size_t length);
    
    // Keep-Alive
    void SetKeepAlive(bool keep_alive) { keep_alive_ = keep_alive; }
    bool IsKeepAlive() const { return keep_alive_; }
    
    // 生成响应（不含 body，body 单独处理）
    void AppendToBuffer(ByteBuffer* buffer) const;
    
    // 重置
    void Reset();

    // 常用响应快捷方法
    static HttpResponse Ok(const std::string& body = "");
    static HttpResponse Json(const std::string& json, int status_code = 200);
    static HttpResponse Error(int status_code, const std::string& code, 
                             const std::string& message, const std::string& trace_id = "");
    static HttpResponse NotFound(const std::string& message = "Not Found", 
                                 const std::string& trace_id = "");
    static HttpResponse BadRequest(const std::string& message = "Bad Request",
                                   const std::string& trace_id = "");
    static HttpResponse Unauthorized(const std::string& message = "Unauthorized",
                                     const std::string& trace_id = "");
    static HttpResponse Forbidden(const std::string& message = "Forbidden",
                                  const std::string& trace_id = "");
    static HttpResponse Conflict(const std::string& message = "Conflict",
                                 const std::string& trace_id = "");
    static HttpResponse InternalError(const std::string& message = "Internal Server Error",
                                      const std::string& trace_id = "");
    static HttpResponse ServiceUnavailable(const std::string& message = "Service Unavailable",
                                           const std::string& trace_id = "");
    static HttpResponse RangeNotSatisfiable(size_t file_size);
    static HttpResponse OK(const std::string& body = "");
    static HttpResponse Created(const std::string& body = "");
    static HttpResponse NoContent();
    
private:
    static std::string GetStatusMessage(int code);
    
    int status_code_ = 200;
    std::string status_message_ = "OK";
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    
    // 文件响应
    std::string file_path_;
    size_t file_size_ = 0;
    size_t file_offset_ = 0;
    size_t file_length_ = 0;  // 0 表示整个文件
    
    bool keep_alive_ = true;
};

} // namespace minis3
