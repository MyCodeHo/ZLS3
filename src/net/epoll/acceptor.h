#pragma once

#include <functional>
#include <memory>
#include <string>

namespace minis3 {

class EventLoop;
class Channel;

/**
 * Acceptor - 监听连接
 * 
 * 负责 listen socket 的管理和新连接的 accept
 */
class Acceptor {
public:
    using NewConnectionCallback = std::function<void(int sockfd, const std::string& peer_addr)>;

    Acceptor(EventLoop* loop, const std::string& ip, uint16_t port, bool reuse_port = true);
    ~Acceptor();

    // 禁止拷贝
    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    /**
     * 设置新连接回调
     */
    void SetNewConnectionCallback(NewConnectionCallback cb) {
        new_connection_callback_ = std::move(cb);
    }

    /**
     * 开始监听
     */
    void Listen();

    /**
     * 是否正在监听
     */
    bool IsListening() const { return listening_; }

    /**
     * 获取监听端口
     */
    uint16_t GetPort() const { return port_; }

private:
    void HandleRead();
    
    static int CreateNonblockingSocket();
    static void SetReuseAddr(int sockfd, bool on);
    static void SetReusePort(int sockfd, bool on);

    EventLoop* loop_;
    int listen_fd_;
    uint16_t port_;
    std::unique_ptr<Channel> channel_;
    NewConnectionCallback new_connection_callback_;
    bool listening_;
    
    // 空闲 fd，用于处理文件描述符耗尽的情况
    int idle_fd_;
};

} // namespace minis3
