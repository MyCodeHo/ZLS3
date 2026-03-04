#include "channel.h"
#include "event_loop.h"
#include <sys/epoll.h>
#include <poll.h>
#include <spdlog/spdlog.h>

namespace minis3 {

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(0)
    , revents_(0)
    , index_(-1)
    , tied_(false)
    , event_handling_(false)
    , added_to_loop_(false) {
}

Channel::~Channel() {
    if (added_to_loop_) {
        // 确保在析构前已从 loop 中移除
    }
}

void Channel::Tie(const std::shared_ptr<void>& obj) {
    // 绑定生命周期，避免回调期间对象被释放
    tie_ = obj;
    tied_ = true;
}

void Channel::Update() {
    // 将当前事件关注状态同步到 EventLoop
    added_to_loop_ = true;
    loop_->UpdateChannel(this);
}

void Channel::Remove() {
    // 从 EventLoop 中移除
    added_to_loop_ = false;
    loop_->RemoveChannel(this);
}

void Channel::HandleEvent() {
    // 若有生命周期绑定，确保对象存活
    if (tied_) {
        std::shared_ptr<void> guard = tie_.lock();
        if (guard) {
            HandleEventWithGuard();
        }
    } else {
        HandleEventWithGuard();
    }
}

void Channel::HandleEventWithGuard() {
    event_handling_ = true;

    // 对端关闭
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
        if (close_callback_) {
            close_callback_();
        }
    }

    // 错误
    if (revents_ & EPOLLERR) {
        if (error_callback_) {
            error_callback_();
        }
    }

    // 可读
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
        if (read_callback_) {
            read_callback_();
        }
    }

    // 可写
    if (revents_ & EPOLLOUT) {
        if (write_callback_) {
            write_callback_();
        }
    }

    event_handling_ = false;
}

void Channel::EnableReading() {
    // 关注读事件
    events_ |= kReadEvent;
    Update();
}

void Channel::DisableReading() {
    // 取消读事件
    events_ &= ~kReadEvent;
    Update();
}

void Channel::EnableWriting() {
    // 关注写事件
    events_ |= kWriteEvent;
    Update();
}

void Channel::DisableWriting() {
    // 取消写事件
    events_ &= ~kWriteEvent;
    Update();
}

void Channel::DisableAll() {
    // 取消所有事件关注
    events_ = kNoneEvent;
    Update();
}

} // namespace minis3
