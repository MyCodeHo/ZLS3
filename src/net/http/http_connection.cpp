#include "http_connection.h"
#include "net/epoll/event_loop.h"
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <spdlog/spdlog.h>

namespace minis3 {

HttpConnection::HttpConnection(EventLoop* loop, int sockfd, const std::string& peer_addr)
    : loop_(loop)
    , sockfd_(sockfd)
    , peer_addr_(peer_addr)
    , state_(ConnectionState::READING_HEADERS)
    , file_fd_(-1)
    , file_offset_(0)
    , file_remaining_(0)
    , idle_timer_id_(0)
    , idle_timeout_(60) {
    
    // 绑定到 Channel，并注册各类事件回调
    channel_ = std::make_unique<Channel>(loop_, sockfd_);
    channel_->SetReadCallback([this] { HandleRead(); });
    channel_->SetWriteCallback([this] { HandleWrite(); });
    channel_->SetCloseCallback([this] { HandleClose(); });
    channel_->SetErrorCallback([this] { HandleError(); });
    
    // 设置 TCP_NODELAY，降低小包延迟
    int optval = 1;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
}

HttpConnection::~HttpConnection() {
    if (file_fd_ >= 0) {
        ::close(file_fd_);
    }
    ::close(sockfd_);
    spdlog::debug("HttpConnection destroyed, peer: {}", peer_addr_);
}

void HttpConnection::SetSocketBufferSizes(int recv_bytes, int send_bytes) {
    // 可选调整 socket 缓冲区大小
    if (recv_bytes > 0) {
        ::setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &recv_bytes, sizeof(recv_bytes));
    }
    if (send_bytes > 0) {
        ::setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, &send_bytes, sizeof(send_bytes));
    }
}

void HttpConnection::Start() {
    // 在所属 EventLoop 中注册读事件与超时
    loop_->RunInLoop([self = shared_from_this()] {
        self->channel_->Tie(self);
        self->channel_->EnableReading();
        self->SetIdleTimeout(self->idle_timeout_);
    });
}

void HttpConnection::Close() {
    loop_->RunInLoop([self = shared_from_this()] {
        self->HandleClose();
    });
}

void HttpConnection::ForceClose() {
    loop_->QueueInLoop([self = shared_from_this()] {
        self->HandleClose();
    });
}

void HttpConnection::SetIdleTimeout(int seconds) {
    // 设置空闲超时定时器
    idle_timeout_ = seconds;
    if (idle_timer_id_ != 0) {
        loop_->CancelTimer(idle_timer_id_);
    }
    
    idle_timer_id_ = loop_->RunAfter(seconds, [weak_self = std::weak_ptr<HttpConnection>(shared_from_this())] {
        if (auto self = weak_self.lock()) {
            spdlog::debug("Connection idle timeout, peer: {}", self->peer_addr_);
            self->ForceClose();
        }
    });
}

void HttpConnection::RefreshIdleTimer() {
    if (idle_timer_id_ != 0) {
        loop_->RefreshTimer(idle_timer_id_, idle_timeout_);
    }
}

void HttpConnection::HandleRead() {
    // 收到数据时刷新空闲超时
    RefreshIdleTimer();
    
    int saved_errno = 0;
    ssize_t n = input_buffer_.ReadFd(sockfd_, &saved_errno);
    
    if (n > 0) {
        // 根据状态机处理请求头或流式 body
        if (state_ == ConnectionState::READING_HEADERS) {
            ProcessHeaders();
        } else if (state_ == ConnectionState::READING_BODY_STREAM) {
            // 流式处理 body
            size_t readable = input_buffer_.ReadableBytes();
            size_t to_process = std::min(readable, parser_.RemainingBodyLength());
            
            if (body_data_callback_) {
                body_data_callback_(shared_from_this(), input_buffer_.Peek(), to_process);
            }
            input_buffer_.Retrieve(to_process);

            // 检查是否完成
            if (parser_.ConsumeBody(to_process)) {
                parser_.Request().SetClientIp(peer_addr_);
                if (request_callback_) {
                    request_callback_(shared_from_this(), parser_.Request());
                }
            }
        }
    } else if (n == 0) {
        HandleClose();
    } else {
        if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK) {
            spdlog::error("Read error on fd {}: {}", sockfd_, strerror(saved_errno));
            HandleError();
        }
    }
}

void HttpConnection::ProcessHeaders() {
    // 解析请求头并推进状态机
    auto result = parser_.Parse(&input_buffer_);
    
    switch (result) {
        case HttpParser::ParseResult::NEED_MORE_DATA:
            // 继续等待更多数据
            break;
            
        case HttpParser::ParseResult::HEADERS_COMPLETE:
            // 头部解析完成，需要读取 body
            if (parser_.HasBody()) {
                if (parser_.ContentLength() > max_body_bytes_) {
                    parser_.Request().SetHeader("Connection", "close");
                    Send(HttpResponse::Error(413, "EntityTooLarge",
                                             "Request body too large", parser_.Request().TraceId()));
                    return;
                }
                // 进入流式 body 读取
                state_ = ConnectionState::READING_BODY_STREAM;
                // 处理已在缓冲区中的 body 数据
                size_t readable = input_buffer_.ReadableBytes();
                if (readable > 0) {
                    size_t to_process = std::min(readable, parser_.RemainingBodyLength());
                    if (body_data_callback_) {
                        body_data_callback_(shared_from_this(), input_buffer_.Peek(), to_process);
                    }
                    input_buffer_.Retrieve(to_process);
                    if (parser_.ConsumeBody(to_process)) {
                        parser_.Request().SetClientIp(peer_addr_);
                        if (request_callback_) {
                            request_callback_(shared_from_this(), parser_.Request());
                        }
                    }
                }
            }
            break;
            
        case HttpParser::ParseResult::MESSAGE_COMPLETE:
            // 请求完整（无 body 或 body 已完成）
            parser_.Request().SetClientIp(peer_addr_);
            if (request_callback_) {
                request_callback_(shared_from_this(), parser_.Request());
            }
            break;
            
        case HttpParser::ParseResult::ERROR:
            spdlog::warn("HTTP parse error: {}", parser_.ErrorMessage());
            Send(HttpResponse::BadRequest(parser_.ErrorMessage()));
            break;
            
        default:
            break;
    }
}

void HttpConnection::HandleWrite() {
    // 发送普通响应或文件
    if (state_ == ConnectionState::SENDING_FILE) {
        SendFileInLoop();
    } else {
        SendInLoop();
    }
}

void HttpConnection::SendInLoop() {
    // 先写缓冲区中的响应头/小 body
    if (output_buffer_.ReadableBytes() == 0) {
        channel_->DisableWriting();
        
        // 检查是否是 keep-alive
        if (parser_.Request().IsKeepAlive()) {
            parser_.Reset();
            state_ = ConnectionState::READING_HEADERS;
        } else {
            HandleClose();
        }
        return;
    }
    
    int saved_errno = 0;
    ssize_t n = output_buffer_.WriteFd(sockfd_, &saved_errno);
    
    if (n < 0) {
        if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK) {
            spdlog::error("Write error: {}", strerror(saved_errno));
            HandleError();
        }
    }
}

void HttpConnection::SendFileInLoop() {
    // 先写响应头，再 sendfile 文件内容
    if (output_buffer_.ReadableBytes() > 0) {
        int saved_errno = 0;
        ssize_t n = output_buffer_.WriteFd(sockfd_, &saved_errno);
        if (n < 0) {
            if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK) {
                spdlog::error("Write header error: {}", strerror(saved_errno));
                HandleError();
            }
            return;
        }

        if (output_buffer_.ReadableBytes() > 0) {
            return;  // 头部未写完，下次继续
        }
    }

    if (file_remaining_ == 0) {
        // 文件已发送完成
        ::close(file_fd_);
        file_fd_ = -1;
        channel_->DisableWriting();
        
        if (parser_.Request().IsKeepAlive()) {
            parser_.Reset();
            state_ = ConnectionState::READING_HEADERS;
        } else {
            HandleClose();
        }
        return;
    }
    
    ssize_t n = ::sendfile(sockfd_, file_fd_, 
        reinterpret_cast<off_t*>(&file_offset_), file_remaining_);
    
    if (n > 0) {
        file_remaining_ -= n;
    } else if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            spdlog::error("sendfile error: {}", strerror(errno));
            HandleError();
        }
    }
}

void HttpConnection::HandleClose() {
    if (state_ == ConnectionState::CLOSED) {
        return;
    }
    
    state_ = ConnectionState::CLOSED;
    
    // 取消定时器
    if (idle_timer_id_ != 0) {
        loop_->CancelTimer(idle_timer_id_);
        idle_timer_id_ = 0;
    }
    
    // 移除事件关注
    channel_->DisableAll();
    
    if (close_callback_) {
        close_callback_(shared_from_this());
    }
}

void HttpConnection::HandleError() {
    int err = 0;
    socklen_t len = sizeof(err);
    if (::getsockopt(sockfd_, SOL_SOCKET, SO_ERROR, &err, &len) == 0) {
        spdlog::error("Connection error on fd {}: {}", sockfd_, strerror(err));
    }
    HandleClose();
}

void HttpConnection::Send(HttpResponse response) {
    if (state_ == ConnectionState::CLOSED) {
        return;
    }
    
    // 在 loop 线程中组装响应并启动写事件
    loop_->RunInLoop([self = shared_from_this(), resp = std::move(response)]() mutable {
        resp.SetKeepAlive(self->parser_.Request().IsKeepAlive());
        resp.AppendToBuffer(&self->output_buffer_);
        
        if (resp.HasFile()) {
            // 文件响应
            self->file_fd_ = ::open(resp.FilePath().c_str(), O_RDONLY);
            if (self->file_fd_ < 0) {
                spdlog::error("Failed to open file: {}", resp.FilePath());
                self->HandleClose();
                return;
            }
            
            self->file_offset_ = resp.FileOffset();
            self->file_remaining_ = resp.FileSize();
            self->state_ = ConnectionState::SENDING_FILE;
        } else {
            self->state_ = ConnectionState::WRITING_RESPONSE;
        }
        
        self->channel_->EnableWriting();
    });
}

void HttpConnection::SendFile(const std::string& file_path, size_t file_size,
                               size_t offset, size_t length) {
    // 组装支持 Range 的文件响应
    HttpResponse response;
    response.SetStatusCode(offset > 0 || length > 0 ? 206 : 200);
    response.SetStatusMessage(offset > 0 || length > 0 ? "Partial Content" : "OK");
    response.SetHeader("Accept-Ranges", "bytes");
    
    size_t actual_length = (length > 0) ? length : file_size;
    response.SetHeader("Content-Length", std::to_string(actual_length));
    
    if (offset > 0 || length > 0) {
        response.SetHeader("Content-Range", 
            "bytes " + std::to_string(offset) + "-" + 
            std::to_string(offset + actual_length - 1) + "/" + 
            std::to_string(file_size));
    }
    
    response.SetFile(file_path, actual_length);
    response.SetFileRange(offset, actual_length);
    
    Send(std::move(response));
}

} // namespace minis3
