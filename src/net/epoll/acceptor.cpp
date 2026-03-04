#include "acceptor.h"
#include "channel.h"
#include "event_loop.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace minis3 {

int Acceptor::CreateNonblockingSocket() {
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sockfd < 0) {
        throw std::runtime_error("Failed to create socket");
    }
    return sockfd;
}

void Acceptor::SetReuseAddr(int sockfd, bool on) {
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
}

void Acceptor::SetReusePort(int sockfd, bool on) {
    int optval = on ? 1 : 0;
    int ret = ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    if (ret < 0 && on) {
        spdlog::warn("SO_REUSEPORT not supported");
    }
}

Acceptor::Acceptor(EventLoop* loop, const std::string& ip, uint16_t port, bool reuse_port)
    : loop_(loop)
    , listen_fd_(CreateNonblockingSocket())
    , port_(port)
    , listening_(false)
    , idle_fd_(::open("/dev/null", O_RDONLY | O_CLOEXEC)) {
    
    // 复用地址/端口，便于快速重启
    SetReuseAddr(listen_fd_, true);
    if (reuse_port) {
        SetReusePort(listen_fd_, true);
    }
    
    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (ip.empty() || ip == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
            ::close(listen_fd_);
            throw std::runtime_error("Invalid IP address: " + ip);
        }
    }
    
    // bind 失败直接抛错
    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error("Failed to bind to " + ip + ":" + std::to_string(port) + 
            " - " + strerror(errno));
    }
    
    // 为监听 fd 创建 Channel 并注册读事件
    channel_ = std::make_unique<Channel>(loop_, listen_fd_);
    channel_->SetReadCallback([this] { HandleRead(); });
}

Acceptor::~Acceptor() {
    channel_->DisableAll();
    channel_->Remove();
    ::close(listen_fd_);
    ::close(idle_fd_);
}

void Acceptor::Listen() {
    loop_->AssertInLoopThread();
    
    // 开始 listen
    listening_ = true;
    if (::listen(listen_fd_, SOMAXCONN) < 0) {
        throw std::runtime_error("Failed to listen: " + std::string(strerror(errno)));
    }
    
    // 关注读事件，accept 新连接
    channel_->EnableReading();
    spdlog::info("Acceptor listening on port {}", port_);
}

void Acceptor::HandleRead() {
    loop_->AssertInLoopThread();
    
    struct sockaddr_in peer_addr;
    socklen_t addr_len = sizeof(peer_addr);
    
    // 循环 accept，直到没有新连接
    while (true) {
        int connfd = ::accept4(listen_fd_, 
            reinterpret_cast<struct sockaddr*>(&peer_addr), 
            &addr_len, 
            SOCK_NONBLOCK | SOCK_CLOEXEC);
        
        if (connfd >= 0) {
            // 格式化客户端地址
            char ip[INET_ADDRSTRLEN];
            ::inet_ntop(AF_INET, &peer_addr.sin_addr, ip, sizeof(ip));
            std::string peer_addr_str = std::string(ip) + ":" + 
                std::to_string(ntohs(peer_addr.sin_port));
            
            spdlog::debug("Accepted connection from {}, fd={}", peer_addr_str, connfd);
            
            // 将新连接交给上层处理
            if (new_connection_callback_) {
                new_connection_callback_(connfd, peer_addr_str);
            } else {
                ::close(connfd);
            }
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有更多连接了
                break;
            } else if (errno == EMFILE) {
                // 文件描述符耗尽，使用空闲 fd 处理
                spdlog::error("File descriptor exhausted");
                ::close(idle_fd_);
                idle_fd_ = ::accept(listen_fd_, nullptr, nullptr);
                ::close(idle_fd_);
                idle_fd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
            } else if (errno != EINTR) {
                spdlog::error("accept4 error: {}", strerror(errno));
                break;
            }
        }
    }
}

} // namespace minis3
