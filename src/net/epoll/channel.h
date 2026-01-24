#pragma once

#include <functional>
#include <memory>

namespace minis3 {

class EventLoop;

/**
 * Channel - fd 与事件回调的绑定
 * 
 * 每个 Channel 对象负责一个 fd 的事件分发，但不拥有 fd
 */
class Channel {
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    // 禁止拷贝
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // 处理事件
    void HandleEvent();

    // 设置回调
    void SetReadCallback(ReadEventCallback cb) { read_callback_ = std::move(cb); }
    void SetWriteCallback(EventCallback cb) { write_callback_ = std::move(cb); }
    void SetCloseCallback(EventCallback cb) { close_callback_ = std::move(cb); }
    void SetErrorCallback(EventCallback cb) { error_callback_ = std::move(cb); }

    // 获取 fd
    int Fd() const { return fd_; }

    // 获取/设置 events
    int Events() const { return events_; }
    void SetRevents(int revents) { revents_ = revents; }

    // 事件注册
    void EnableReading();
    void DisableReading();
    void EnableWriting();
    void DisableWriting();
    void DisableAll();

    // 状态检查
    bool IsNoneEvent() const { return events_ == kNoneEvent; }
    bool IsWriting() const { return events_ & kWriteEvent; }
    bool IsReading() const { return events_ & kReadEvent; }

    // 用于 epoll
    int Index() const { return index_; }
    void SetIndex(int idx) { index_ = idx; }

    // 所属 EventLoop
    EventLoop* OwnerLoop() { return loop_; }

    // 从 EventLoop 中移除
    void Remove();

    // 生命周期绑定
    void Tie(const std::shared_ptr<void>& obj);

private:
    void Update();
    void HandleEventWithGuard();

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* loop_;
    const int fd_;
    int events_;    // 注册的事件
    int revents_;   // epoll 返回的就绪事件
    int index_;     // 在 epoll 中的状态（新增/已添加/已删除）

    std::weak_ptr<void> tie_;
    bool tied_;
    bool event_handling_;
    bool added_to_loop_;

    ReadEventCallback read_callback_;
    EventCallback write_callback_;
    EventCallback close_callback_;
    EventCallback error_callback_;
};

} // namespace minis3
