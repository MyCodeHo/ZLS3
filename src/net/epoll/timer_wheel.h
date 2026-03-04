#pragma once

#include <functional>
#include <vector>
#include <memory>
#include <unordered_set>
#include <cstdint>

namespace minis3 {

/**
 * TimerWheel - 时间轮定时器
 * 
 * 用于管理连接超时等定时任务
 */
class TimerWheel {
public:
    using TimerCallback = std::function<void()>;
    using TimerId = uint64_t;

    /**
     * @param wheel_size 时间轮槽数（秒）
     */
    explicit TimerWheel(size_t wheel_size = 60);
    ~TimerWheel();

    /**
     * 添加定时器
     * @param delay_seconds 延迟秒数
     * @param callback 回调函数
     * @param repeat 是否重复
     * @return 定时器 ID
     */
    TimerId AddTimer(int delay_seconds, TimerCallback callback, bool repeat = false);

    /**
     * 取消定时器
     */
    void CancelTimer(TimerId id);

    /**
     * 推进时间（每秒调用一次）
     */
    void Tick();

    /**
     * 刷新定时器（重置延迟）
     */
    void RefreshTimer(TimerId id, int delay_seconds);

private:
    struct TimerEntry {
        TimerId id;
        TimerCallback callback;
        bool repeat;
        int interval;
        bool cancelled;
    };

    using Bucket = std::vector<std::shared_ptr<TimerEntry>>;

    size_t wheel_size_;
    size_t current_slot_;
    std::vector<Bucket> wheel_;
    std::unordered_map<TimerId, std::weak_ptr<TimerEntry>> timer_map_;
    TimerId next_timer_id_;
};

} // namespace minis3
