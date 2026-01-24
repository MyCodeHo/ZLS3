#pragma once

#include <sys/eventfd.h>

namespace minis3 {

class EventLoop;
class Channel;

/**
 * Notifier - 使用 eventfd 实现跨线程唤醒
 */
class Notifier {
public:
    explicit Notifier(EventLoop* loop);
    ~Notifier();

    // 禁止拷贝
    Notifier(const Notifier&) = delete;
    Notifier& operator=(const Notifier&) = delete;

    /**
     * 唤醒 EventLoop（线程安全）
     */
    void Notify();

private:
    void HandleRead();
    
    static int CreateEventFd();

    EventLoop* loop_;
    int event_fd_;
    Channel* channel_;
};

} // namespace minis3
