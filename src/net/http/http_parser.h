#pragma once

#include "http_request.h"
#include <string_view>
#include <optional>

namespace minis3 {

class ByteBuffer;

/**
 * HTTP 解析器
 * 
 * 支持流式解析 HTTP/1.1 请求
 */
class HttpParser {
public:
    enum class ParseState {
        REQUEST_LINE,
        HEADERS,
        BODY,
        DONE,
        ERROR
    };
    
    enum class ParseResult {
        NEED_MORE_DATA,     // 需要更多数据
        HEADERS_COMPLETE,   // 头部解析完成
        BODY_DATA,          // 有 body 数据
        MESSAGE_COMPLETE,   // 消息完整
        ERROR               // 解析错误
    };
    
    HttpParser();
    
    /**
     * 解析缓冲区中的数据
     * @param buffer 输入缓冲区
     * @return 解析结果
     */
    ParseResult Parse(ByteBuffer* buffer);
    
    /**
     * 获取解析后的请求
     */
    HttpRequest& Request() { return request_; }
    const HttpRequest& Request() const { return request_; }
    
    /**
     * 获取当前状态
     */
    ParseState State() const { return state_; }
    
    /**
     * 获取错误信息
     */
    const std::string& ErrorMessage() const { return error_message_; }
    
    /**
     * 重置解析器（用于 keep-alive 连接）
     */
    void Reset();
    
    /**
     * 是否需要读取 body
     */
    bool HasBody() const;

    /**
     * 获取 Content-Length
     */
    size_t ContentLength() const { return content_length_; }
    
    /**
     * 获取剩余需要读取的 body 长度
     */
    size_t RemainingBodyLength() const { return remaining_body_length_; }

    /**
     * 手动消费 body 字节（用于流式处理）
     */
    bool ConsumeBody(size_t len) {
        if (len >= remaining_body_length_) {
            remaining_body_length_ = 0;
            state_ = ParseState::DONE;
            return true;
        }
        remaining_body_length_ -= len;
        return false;
    }
    
    /**
     * 是否是 chunked 编码
     */
    bool IsChunked() const { return chunked_; }

private:
    bool ParseRequestLine(std::string_view line);
    bool ParseHeader(std::string_view line);
    void FinishHeaders();
    
    ParseState state_;
    HttpRequest request_;
    std::string error_message_;
    
    size_t content_length_;
    size_t remaining_body_length_;
    bool chunked_;
    
    // chunked 解析状态
    size_t current_chunk_size_;
    bool reading_chunk_size_;
    
    static constexpr size_t kMaxHeaderSize = 8 * 1024;
    static constexpr size_t kMaxRequestLineSize = 4 * 1024;
};

} // namespace minis3
