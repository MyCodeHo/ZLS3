#pragma once

#include "http_parser.h"
#include "http_request.h"
#include "http_response.h"
#include "net/buffer/byte_buffer.h"
#include "net/epoll/channel.h"
#include <memory>
#include <functional>

namespace minis3 {

class EventLoop;
class HttpRouter;

/**
 * HTTP 连接状态
 */
enum class ConnectionState {
    READING_HEADERS,      // 读取请求头
    READING_BODY_STREAM,  // 流式读取 body（大文件上传）
    PROCESSING,           // 处理中（已投递到 worker）
    WRITING_RESPONSE,     // 写入响应
    SENDING_FILE,         // 发送文件（sendfile）
    CLOSED                // 已关闭
};

/**
 * HTTP 连接
 * 
 * 管理单个客户端连接的生命周期和状态
 */
class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
    using CloseCallback = std::function<void(const std::shared_ptr<HttpConnection>&)>;
    using RequestCallback = std::function<void(const std::shared_ptr<HttpConnection>&, HttpRequest&)>;
    using BodyDataCallback = std::function<void(const std::shared_ptr<HttpConnection>&, 
                                                const char* data, size_t len)>;

    HttpConnection(EventLoop* loop, int sockfd, const std::string& peer_addr);
    ~HttpConnection();

    // 禁止拷贝
    HttpConnection(const HttpConnection&) = delete;
    HttpConnection& operator=(const HttpConnection&) = delete;

    /**
     * 初始化连接
     */
    void Start();

    /**
     * 关闭连接
     */
    void Close();

    /**
     * 强制关闭
     */
    void ForceClose();

    /**
     * 发送响应
     */
    void Send(HttpResponse response);

    /**
     * 发送文件
     */
    void SendFile(const std::string& file_path, size_t file_size,
                  size_t offset = 0, size_t length = 0);

    // 回调设置
    void SetCloseCallback(CloseCallback cb) { close_callback_ = std::move(cb); }
    void SetRequestCallback(RequestCallback cb) { request_callback_ = std::move(cb); }
    void SetBodyDataCallback(BodyDataCallback cb) { body_data_callback_ = std::move(cb); }

    // 状态获取
    ConnectionState State() const { return state_; }
    const std::string& PeerAddr() const { return peer_addr_; }
    int Fd() const { return sockfd_; }
    EventLoop* GetLoop() const { return loop_; }
    HttpRequest& Request() { return parser_.Request(); }
    
    // 定时器相关
    void SetIdleTimeout(int seconds);
    void RefreshIdleTimer();

    // 限制请求体大小
    void SetMaxBodyBytes(size_t bytes) { max_body_bytes_ = bytes; }

    // Socket 缓冲区
    void SetSocketBufferSizes(int recv_bytes, int send_bytes);

private:
    void HandleRead();
    void HandleWrite();
    void HandleClose();
    void HandleError();

    void ProcessHeaders();
    void ProcessBodyData(const char* data, size_t len);
    
    void SendInLoop();
    void SendFileInLoop();

    EventLoop* loop_;
    int sockfd_;
    std::string peer_addr_;
    std::unique_ptr<Channel> channel_;
    
    ConnectionState state_;
    HttpParser parser_;
    
    ByteBuffer input_buffer_;
    ByteBuffer output_buffer_;
    
    // 文件发送
    int file_fd_;
    size_t file_offset_;
    size_t file_remaining_;
    
    // 回调
    CloseCallback close_callback_;
    RequestCallback request_callback_;
    BodyDataCallback body_data_callback_;
    
    // 定时器
    uint64_t idle_timer_id_;
    int idle_timeout_;

    size_t max_body_bytes_ = static_cast<size_t>(-1);

    static constexpr size_t kHighWaterMark = 64 * 1024 * 1024;  // 64MB
};

} // namespace minis3
