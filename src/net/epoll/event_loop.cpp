#include "event_loop.h"
#include "channel.h"
#include "notifier.h"
#include "timer_wheel.h"
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace minis3 {

// 线程局部变量，存储当前线程的 EventLoop
thread_local EventLoop* t_loop_in_this_thread = nullptr;

EventLoop* EventLoop::GetEventLoopOfCurrentThread() {
    return t_loop_in_this_thread;
}

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , calling_pending_functors_(false)
    , thread_id_(std::this_thread::get_id())
    , events_(kInitEventListSize) {
    
    // 检查一个线程只能有一个 EventLoop
    if (t_loop_in_this_thread) {
        throw std::runtime_error("Another EventLoop exists in this thread");
    }
    t_loop_in_this_thread = this;
    
    // 创建 epoll
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("Failed to create epoll fd");
    }
    
    // 创建 notifier（用于跨线程唤醒）
    notifier_ = std::make_unique<Notifier>(this);
    
    // 创建定时器
    timer_wheel_ = std::make_unique<TimerWheel>(3600);  // 1小时的时间轮
    
    // 创建 timerfd（每秒触发一次，驱动时间轮）
    timer_fd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer_fd_ < 0) {
        throw std::runtime_error("Failed to create timerfd");
    }
    
    // 设置 timerfd 每秒触发
    struct itimerspec ts;
    ts.it_value.tv_sec = 1;
    ts.it_value.tv_nsec = 0;
    ts.it_interval.tv_sec = 1;
    ts.it_interval.tv_nsec = 0;
    ::timerfd_settime(timer_fd_, 0, &ts, nullptr);
    
    // 监听 timerfd
    timer_channel_ = std::make_unique<Channel>(this, timer_fd_);
    timer_channel_->SetReadCallback([this] { HandleTimerTick(); });
    timer_channel_->EnableReading();
    
    spdlog::debug("EventLoop created in thread {}", 
        std::hash<std::thread::id>{}(thread_id_));
}

EventLoop::~EventLoop() {
    if (timer_channel_) {
        timer_channel_->DisableAll();
        timer_channel_->Remove();
    }
    // 先销毁 notifier，避免在 epoll 关闭后执行 DEL
    notifier_.reset();
    if (timer_fd_ >= 0) {
        ::close(timer_fd_);
        timer_fd_ = -1;
    }
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
    t_loop_in_this_thread = nullptr;
}

void EventLoop::Loop() {
    looping_ = true;
    quit_ = false;
    
    spdlog::info("EventLoop {} start looping", 
        std::hash<std::thread::id>{}(thread_id_));
    
    // 主循环：等待 epoll 事件并处理任务队列
    while (!quit_) {
        int num_events = ::epoll_wait(epoll_fd_, events_.data(), 
            static_cast<int>(events_.size()), kEpollTimeout);
        
        if (num_events > 0) {
            // 分发事件给对应 Channel
            for (int i = 0; i < num_events; ++i) {
                Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
                channel->SetRevents(events_[i].events);
                channel->HandleEvent();
            }
            
            // 如果事件数量达到上限，扩容
            if (static_cast<size_t>(num_events) == events_.size()) {
                events_.resize(events_.size() * 2);
            }
        } else if (num_events < 0) {
            if (errno != EINTR) {
                spdlog::error("epoll_wait error: {}", strerror(errno));
            }
        }
        
        // 执行待处理的任务
        DoPendingFunctors();
    }
    
    looping_ = false;
    spdlog::info("EventLoop {} stop looping", 
        std::hash<std::thread::id>{}(thread_id_));
}

void EventLoop::Quit() {
    // 设置退出标记，必要时唤醒 loop 线程
    quit_ = true;
    if (!IsInLoopThread()) {
        Wakeup();
    }
}

void EventLoop::RunInLoop(Functor cb) {
    // 同线程立即执行，跨线程则投递
    if (IsInLoopThread()) {
        cb();
    } else {
        QueueInLoop(std::move(cb));
    }
}

void EventLoop::QueueInLoop(Functor cb) {
    // 加入待执行队列
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_functors_.push_back(std::move(cb));
    }
    
    // 非 loop 线程或正在执行回调时需要唤醒
    if (!IsInLoopThread() || calling_pending_functors_) {
        Wakeup();
    }
}

void EventLoop::Wakeup() {
    // 通过 notifier 唤醒 epoll_wait
    notifier_->Notify();
}

EventLoop::TimerId EventLoop::RunAfter(int delay_seconds, Functor cb, bool repeat) {
    return timer_wheel_->AddTimer(delay_seconds, std::move(cb), repeat);
}

void EventLoop::CancelTimer(TimerId id) {
    timer_wheel_->CancelTimer(id);
}

void EventLoop::RefreshTimer(TimerId id, int delay_seconds) {
    timer_wheel_->RefreshTimer(id, delay_seconds);
}

void EventLoop::UpdateChannel(Channel* channel) {
    AssertInLoopThread();
    
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = channel->Events();
    event.data.ptr = channel;
    int fd = channel->Fd();
    
    if (channel->Index() < 0) {
        // 新 channel，添加到 epoll
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) < 0) {
            spdlog::error("epoll_ctl ADD fd {} error: {}", fd, strerror(errno));
            return;
        }
        channel->SetIndex(1);  // 标记为已添加
        channels_[fd] = channel;
    } else {
        // 已存在的 channel，修改
        if (channel->IsNoneEvent()) {
            // 如果没有监听任何事件，从 epoll 中删除
            if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &event) < 0) {
                spdlog::error("epoll_ctl DEL fd {} error: {}", fd, strerror(errno));
            }
            channel->SetIndex(-1);
        } else {
            if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) < 0) {
                spdlog::error("epoll_ctl MOD fd {} error: {}", fd, strerror(errno));
            }
        }
    }
}

void EventLoop::RemoveChannel(Channel* channel) {
    AssertInLoopThread();
    
    int fd = channel->Fd();
    channels_.erase(fd);
    
    if (channel->Index() >= 0) {
        struct epoll_event event;
        // 从 epoll 中删除
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &event) < 0) {
            spdlog::error("epoll_ctl DEL fd {} error: {}", fd, strerror(errno));
        }
    }
    channel->SetIndex(-1);
}

bool EventLoop::HasChannel(Channel* channel) {
    AssertInLoopThread();
    auto it = channels_.find(channel->Fd());
    return it != channels_.end() && it->second == channel;
}

bool EventLoop::IsInLoopThread() const {
    return thread_id_ == std::this_thread::get_id();
}

void EventLoop::AssertInLoopThread() {
    if (!IsInLoopThread()) {
        spdlog::critical("EventLoop was created in thread {}, current thread is {}",
            std::hash<std::thread::id>{}(thread_id_),
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::abort();
    }
}

void EventLoop::DoPendingFunctors() {
    // 批量取出队列，减少锁持有时间
    std::vector<Functor> functors;
    calling_pending_functors_ = true;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pending_functors_);
    }
    
    for (const auto& functor : functors) {
        functor();
    }
    
    calling_pending_functors_ = false;
}

void EventLoop::HandleTimerTick() {
    // 读取 timerfd（必须读取，否则会一直触发）
    uint64_t exp;
    ::read(timer_fd_, &exp, sizeof(exp));
    
    // 推进时间轮
    timer_wheel_->Tick();
}

} // namespace minis3
