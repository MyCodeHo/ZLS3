#include "http_parser.h"
#include "net/buffer/byte_buffer.h"
#include <algorithm>
#include <charconv>

namespace minis3 {

HttpParser::HttpParser()
    : state_(ParseState::REQUEST_LINE)
    , content_length_(0)
    , remaining_body_length_(0)
    , chunked_(false)
    , current_chunk_size_(0)
    , reading_chunk_size_(true) {
}

void HttpParser::Reset() {
    state_ = ParseState::REQUEST_LINE;
    request_.Reset();
    error_message_.clear();
    content_length_ = 0;
    remaining_body_length_ = 0;
    chunked_ = false;
    current_chunk_size_ = 0;
    reading_chunk_size_ = true;
}

bool HttpParser::HasBody() const {
    return content_length_ > 0 || chunked_;
}

HttpParser::ParseResult HttpParser::Parse(ByteBuffer* buffer) {
    // 状态机驱动的流式解析
    while (true) {
        switch (state_) {
            case ParseState::REQUEST_LINE: {
                const char* crlf = buffer->FindCRLF();
                if (!crlf) {
                    if (buffer->ReadableBytes() > kMaxRequestLineSize) {
                        error_message_ = "Request line too long";
                        state_ = ParseState::ERROR;
                        return ParseResult::ERROR;
                    }
                    // 请求行不完整，等待更多数据
                    return ParseResult::NEED_MORE_DATA;
                }
                
                std::string_view line(buffer->Peek(), crlf - buffer->Peek());
                if (!ParseRequestLine(line)) {
                    state_ = ParseState::ERROR;
                    return ParseResult::ERROR;
                }
                
                // 消费请求行
                buffer->RetrieveUntil(crlf + 2);
                state_ = ParseState::HEADERS;
                break;
            }
            
            case ParseState::HEADERS: {
                const char* crlf = buffer->FindCRLF();
                if (!crlf) {
                    if (buffer->ReadableBytes() > kMaxHeaderSize) {
                        error_message_ = "Headers too large";
                        state_ = ParseState::ERROR;
                        return ParseResult::ERROR;
                    }
                    // 头部未完整，等待更多数据
                    return ParseResult::NEED_MORE_DATA;
                }
                
                std::string_view line(buffer->Peek(), crlf - buffer->Peek());
                
                if (line.empty()) {
                    // 空行，headers 结束
                    buffer->RetrieveUntil(crlf + 2);
                    FinishHeaders();
                    
                    if (HasBody()) {
                        // 进入 body 解析
                        state_ = ParseState::BODY;
                        return ParseResult::HEADERS_COMPLETE;
                    } else {
                        // 无 body，请求完成
                        state_ = ParseState::DONE;
                        return ParseResult::MESSAGE_COMPLETE;
                    }
                }
                
                if (!ParseHeader(line)) {
                    state_ = ParseState::ERROR;
                    return ParseResult::ERROR;
                }
                
                // 消费该 header 行
                buffer->RetrieveUntil(crlf + 2);
                break;
            }
            
            case ParseState::BODY: {
                if (chunked_) {
                    // TODO: 实现 chunked 解析
                    // 目前简化处理，不支持 chunked
                    error_message_ = "Chunked encoding not supported yet";
                    state_ = ParseState::ERROR;
                    return ParseResult::ERROR;
                }
                
                // 普通 body
                size_t readable = buffer->ReadableBytes();
                if (readable == 0) {
                    // 需要更多 body 数据
                    return ParseResult::NEED_MORE_DATA;
                }
                
                size_t to_read = std::min(readable, remaining_body_length_);
                // 不在这里存储 body，由上层处理（流式）
                remaining_body_length_ -= to_read;
                
                if (remaining_body_length_ == 0) {
                    // body 已读完
                    state_ = ParseState::DONE;
                    return ParseResult::MESSAGE_COMPLETE;
                }
                
                // body 还有剩余，通知上层继续处理
                return ParseResult::BODY_DATA;
            }
            
            case ParseState::DONE:
                return ParseResult::MESSAGE_COMPLETE;
                
            case ParseState::ERROR:
                return ParseResult::ERROR;
        }
    }
}

bool HttpParser::ParseRequestLine(std::string_view line) {
    // 格式: METHOD PATH HTTP/VERSION
    // 例如: GET /index.html HTTP/1.1
    
    // 找第一个空格（方法结束）
    auto space1 = line.find(' ');
    if (space1 == std::string_view::npos) {
        error_message_ = "Invalid request line: missing method";
        return false;
    }
    
    std::string_view method = line.substr(0, space1);
    request_.SetMethod(StringToMethod(method));
    
    if (request_.Method() == HttpMethod::UNKNOWN) {
        error_message_ = "Unknown HTTP method";
        return false;
    }
    
    // 找第二个空格（路径结束）
    auto space2 = line.find(' ', space1 + 1);
    if (space2 == std::string_view::npos) {
        error_message_ = "Invalid request line: missing path";
        return false;
    }
    
    std::string_view uri = line.substr(space1 + 1, space2 - space1 - 1);
    
    // 分离 path 和 query
    auto query_pos = uri.find('?');
    if (query_pos != std::string_view::npos) {
        request_.SetPath(std::string(uri.substr(0, query_pos)));
        request_.SetQuery(std::string(uri.substr(query_pos + 1)));
    } else {
        request_.SetPath(std::string(uri));
    }
    
    // 版本
    std::string_view version = line.substr(space2 + 1);
    if (version != "HTTP/1.0" && version != "HTTP/1.1") {
        error_message_ = "Unsupported HTTP version";
        return false;
    }
    request_.SetVersion(std::string(version));
    
    return true;
}

bool HttpParser::ParseHeader(std::string_view line) {
    // 格式: Key: Value
    auto colon = line.find(':');
    if (colon == std::string_view::npos) {
        error_message_ = "Invalid header: missing colon";
        return false;
    }
    
    std::string_view key = line.substr(0, colon);
    std::string_view value = line.substr(colon + 1);
    
    // 去除 value 前后的空白
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    
    request_.AddHeader(std::string(key), std::string(value));
    return true;
}

void HttpParser::FinishHeaders() {
    // 检查 Content-Length
    auto content_length_header = request_.GetHeader("content-length");
    if (!content_length_header.empty()) {
        auto result = std::from_chars(
            content_length_header.data(),
            content_length_header.data() + content_length_header.size(),
            content_length_);
        if (result.ec != std::errc()) {
            content_length_ = 0;
        }
        remaining_body_length_ = content_length_;
    }
    
    // 检查 Transfer-Encoding
    auto transfer_encoding = request_.GetHeader("transfer-encoding");
    if (!transfer_encoding.empty() && transfer_encoding.find("chunked") != std::string::npos) {
        chunked_ = true;
    }
}

} // namespace minis3
