#include "notifier.h"
#include "channel.h"
#include "event_loop.h"
#include <unistd.h>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace minis3 {

int Notifier::CreateEventFd() {
    // 创建 eventfd 用于跨线程唤醒
    int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("Failed to create eventfd");
    }
    return fd;
}

Notifier::Notifier(EventLoop* loop)
    : loop_(loop)
    , event_fd_(CreateEventFd())
    , channel_(new Channel(loop, event_fd_)) {
    // 监听 eventfd 可读事件
    channel_->SetReadCallback([this] { HandleRead(); });
    channel_->EnableReading();
}

Notifier::~Notifier() {
    channel_->DisableAll();
    channel_->Remove();
    delete channel_;
    ::close(event_fd_);
}

void Notifier::Notify() {
    // 写入 eventfd 触发唤醒
    uint64_t one = 1;
    ssize_t n = ::write(event_fd_, &one, sizeof(one));
    if (n != sizeof(one)) {
        spdlog::error("Notifier::Notify() writes {} bytes instead of 8", n);
    }
}

void Notifier::HandleRead() {
    // 读取 eventfd 清空事件
    uint64_t one = 0;
    ssize_t n = ::read(event_fd_, &one, sizeof(one));
    if (n != sizeof(one)) {
        spdlog::error("Notifier::HandleRead() reads {} bytes instead of 8", n);
    }
}

} // namespace minis3
