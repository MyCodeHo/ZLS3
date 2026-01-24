#pragma once

#include <string>
#include <variant>
#include <optional>
#include <string_view>

namespace minis3 {

/**
 * 错误码枚举
 */
enum class ErrorCode {
    OK = 0,
    
    // 客户端错误 4xx
    InvalidArgument = 400001,
    InvalidRange = 400002,
    InvalidPartNumber = 400003,
    InvalidPartOrder = 400004,
    EntityTooLarge = 400005,
    ChecksumMismatch = 400006,
    MissingContentLength = 400007,
    
    Unauthorized = 401001,
    InvalidToken = 401002,
    
    AccessDenied = 403001,
    SignatureExpired = 403002,
    
    NoSuchBucket = 404001,
    NoSuchKey = 404002,
    NoSuchUpload = 404003,
    
    BucketAlreadyExists = 409001,
    IdempotencyConflict = 409002,
    ObjectAlreadyExists = 409003,
    
    // 服务端错误 5xx
    InternalError = 500001,
    DatabaseError = 500002,
    StorageError = 500003,
    
    ServiceUnavailable = 503001,
};

/**
 * 获取错误码对应的 HTTP 状态码
 */
inline int GetHttpStatus(ErrorCode code) {
    int code_value = static_cast<int>(code);
    if (code_value == 0) return 200;
    return code_value / 1000;
}

/**
 * 获取错误码名称
 */
inline std::string_view GetErrorName(ErrorCode code) {
    switch (code) {
        case ErrorCode::OK: return "OK";
        case ErrorCode::InvalidArgument: return "InvalidArgument";
        case ErrorCode::InvalidRange: return "InvalidRange";
        case ErrorCode::InvalidPartNumber: return "InvalidPartNumber";
        case ErrorCode::InvalidPartOrder: return "InvalidPartOrder";
        case ErrorCode::EntityTooLarge: return "EntityTooLarge";
        case ErrorCode::ChecksumMismatch: return "ChecksumMismatch";
        case ErrorCode::MissingContentLength: return "MissingContentLength";
        case ErrorCode::Unauthorized: return "Unauthorized";
        case ErrorCode::InvalidToken: return "InvalidToken";
        case ErrorCode::AccessDenied: return "AccessDenied";
        case ErrorCode::SignatureExpired: return "SignatureExpired";
        case ErrorCode::NoSuchBucket: return "NoSuchBucket";
        case ErrorCode::NoSuchKey: return "NoSuchKey";
        case ErrorCode::NoSuchUpload: return "NoSuchUpload";
        case ErrorCode::BucketAlreadyExists: return "BucketAlreadyExists";
        case ErrorCode::IdempotencyConflict: return "IdempotencyConflict";
        case ErrorCode::ObjectAlreadyExists: return "ObjectAlreadyExists";
        case ErrorCode::InternalError: return "InternalError";
        case ErrorCode::DatabaseError: return "DatabaseError";
        case ErrorCode::StorageError: return "StorageError";
        case ErrorCode::ServiceUnavailable: return "ServiceUnavailable";
        default: return "Unknown";
    }
}

/**
 * Status 类 - 用于表示操作结果
 */
class Status {
public:
    Status() : code_(ErrorCode::OK) {}
    Status(ErrorCode code) : code_(code) {}
    Status(ErrorCode code, std::string message) 
        : code_(code), message_(std::move(message)) {}
    
    static Status OK() { return Status(); }
    static Status Error(ErrorCode code, std::string message = "") {
        return Status(code, std::move(message));
    }
    
    // 便捷构造方法
    static Status InvalidArgument(const std::string& msg = "") {
        return Status(ErrorCode::InvalidArgument, msg);
    }
    static Status NotFound(const std::string& msg = "") {
        return Status(ErrorCode::NoSuchKey, msg);
    }
    static Status AlreadyExists(const std::string& msg = "") {
        return Status(ErrorCode::BucketAlreadyExists, msg);
    }
    static Status Unauthorized(const std::string& msg = "") {
        return Status(ErrorCode::Unauthorized, msg);
    }
    static Status InternalError(const std::string& msg = "") {
        return Status(ErrorCode::InternalError, msg);
    }
    
    bool ok() const { return code_ == ErrorCode::OK; }
    bool IsOK() const { return ok(); }
    bool IsNotFound() const { 
        return code_ == ErrorCode::NoSuchKey || 
               code_ == ErrorCode::NoSuchBucket ||
               code_ == ErrorCode::NoSuchUpload;
    }
    
    ErrorCode code() const { return code_; }
    const std::string& message() const { return message_; }
    
    int HttpStatus() const { return GetHttpStatus(code_); }
    std::string_view ErrorName() const { return GetErrorName(code_); }
    
    std::string ToString() const {
        if (ok()) return "OK";
        return std::string(GetErrorName(code_)) + ": " + message_;
    }

private:
    ErrorCode code_;
    std::string message_;
};

/**
 * Result<T> - 带返回值的结果类型
 */
template<typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(Status status) : data_(std::move(status)) {}
    Result(ErrorCode code, std::string message = "") 
        : data_(Status(code, std::move(message))) {}
    
    // 静态构造方法
    static Result<T> Ok(T value) { return Result<T>(std::move(value)); }
    static Result<T> Err(Status status) { return Result<T>(std::move(status)); }
    static Result<T> Err(ErrorCode code, std::string message = "") { 
        return Result<T>(code, std::move(message)); 
    }
    
    bool ok() const { return std::holds_alternative<T>(data_); }
    bool IsOK() const { return ok(); }
    
    const T& value() const { return std::get<T>(data_); }
    T& value() { return std::get<T>(data_); }
    
    T&& MoveValue() { return std::move(std::get<T>(data_)); }
    
    const Status& status() const { 
        return ok() ? ok_status_ : std::get<Status>(data_); 
    }
    
    ErrorCode code() const { 
        return ok() ? ErrorCode::OK : std::get<Status>(data_).code(); 
    }
    
    // 便捷操作符
    explicit operator bool() const { return ok(); }
    const T* operator->() const { return &value(); }
    T* operator->() { return &value(); }
    const T& operator*() const { return value(); }
    T& operator*() { return value(); }

private:
    std::variant<T, Status> data_;
    static inline Status ok_status_ = Status::OK();
};

} // namespace minis3
