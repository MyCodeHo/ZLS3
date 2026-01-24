#pragma once

#include <functional>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>
#include <sys/epoll.h>

namespace minis3 {

class Channel;
class Notifier;
class TimerWheel;

/**
 * EventLoop - 事件循环
 * 
 * 每个 IO 线程拥有一个 EventLoop，负责：
 * - epoll_wait 等待事件
 * - 分发事件到对应的 Channel
 * - 执行跨线程投递的任务
 */
class EventLoop {
public:
    using Functor = std::function<void()>;
    using TimerId = uint64_t;

    EventLoop();
    ~EventLoop();

    // 禁止拷贝
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /**
     * 开始事件循环（阻塞）
     */
    void Loop();

    /**
     * 退出事件循环
     */
    void Quit();

    /**
     * 在 loop 线程中执行任务
     * 如果当前在 loop 线程，立即执行；否则投递到队列
     */
    void RunInLoop(Functor cb);

    /**
     * 投递任务到队列（不立即执行）
     */
    void QueueInLoop(Functor cb);

    /**
     * 唤醒 loop 线程
     */
    void Wakeup();

    /**
     * 添加定时器
     * @param delay_seconds 延迟秒数
     * @param cb 回调函数
     * @param repeat 是否重复
     * @return 定时器 ID
     */
    TimerId RunAfter(int delay_seconds, Functor cb, bool repeat = false);

    /**
     * 取消定时器
     */
    void CancelTimer(TimerId id);

    /**
     * 刷新定时器
     */
    void RefreshTimer(TimerId id, int delay_seconds = 0);

    // Channel 管理
    void UpdateChannel(Channel* channel);
    void RemoveChannel(Channel* channel);
    bool HasChannel(Channel* channel);

    // 线程检查
    bool IsInLoopThread() const;
    void AssertInLoopThread();

    // 获取 epoll fd（用于调试）
    int GetEpollFd() const { return epoll_fd_; }

    // 静态方法：获取当前线程的 EventLoop
    static EventLoop* GetEventLoopOfCurrentThread();

private:
    void DoPendingFunctors();
    void HandleTimerTick();

    std::atomic<bool> looping_;
    std::atomic<bool> quit_;
    std::atomic<bool> calling_pending_functors_;
    
    const std::thread::id thread_id_;
    int epoll_fd_;
    
    std::unique_ptr<Notifier> notifier_;
    std::unique_ptr<TimerWheel> timer_wheel_;
    int timer_fd_;
    std::unique_ptr<Channel> timer_channel_;
    
    std::vector<struct epoll_event> events_;
    std::unordered_map<int, Channel*> channels_;
    
    std::mutex mutex_;
    std::vector<Functor> pending_functors_;

    static constexpr int kInitEventListSize = 16;
    static constexpr int kEpollTimeout = 10000;  // 10 秒
};

} // namespace minis3
