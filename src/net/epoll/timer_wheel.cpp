#include "timer_wheel.h"
#include <algorithm>

namespace minis3 {

TimerWheel::TimerWheel(size_t wheel_size)
    : wheel_size_(wheel_size)
    , current_slot_(0)
    , wheel_(wheel_size)
    , next_timer_id_(1) {
}

TimerWheel::~TimerWheel() = default;

TimerWheel::TimerId TimerWheel::AddTimer(int delay_seconds, TimerCallback callback, bool repeat) {
    // 添加定时器（最小延迟 1s）
    if (delay_seconds <= 0) {
        delay_seconds = 1;
    }
    
    auto entry = std::make_shared<TimerEntry>();
    entry->id = next_timer_id_++;
    entry->callback = std::move(callback);
    entry->repeat = repeat;
    entry->interval = delay_seconds;
    entry->cancelled = false;
    
    // 计算目标槽位
    size_t target_slot = (current_slot_ + delay_seconds) % wheel_size_;
    wheel_[target_slot].push_back(entry);
    timer_map_[entry->id] = entry;
    
    return entry->id;
}

void TimerWheel::CancelTimer(TimerId id) {
    // 取消定时器
    auto it = timer_map_.find(id);
    if (it != timer_map_.end()) {
        if (auto entry = it->second.lock()) {
            entry->cancelled = true;
        }
        timer_map_.erase(it);
    }
}

void TimerWheel::RefreshTimer(TimerId id, int delay_seconds) {
    // 重新设置定时器（生成新 entry）
    auto it = timer_map_.find(id);
    if (it == timer_map_.end()) {
        return;
    }
    
    auto entry = it->second.lock();
    if (!entry || entry->cancelled) {
        return;
    }
    
    // 创建新的 entry（旧的会在 Tick 时被清理）
    auto new_entry = std::make_shared<TimerEntry>();
    new_entry->id = entry->id;
    new_entry->callback = std::move(entry->callback);
    new_entry->repeat = entry->repeat;
    new_entry->interval = delay_seconds > 0 ? delay_seconds : entry->interval;
    new_entry->cancelled = false;
    
    // 标记旧的为已取消
    entry->cancelled = true;
    
    // 添加到新槽位
    size_t target_slot = (current_slot_ + new_entry->interval) % wheel_size_;
    wheel_[target_slot].push_back(new_entry);
    timer_map_[new_entry->id] = new_entry;
}

void TimerWheel::Tick() {
    // 推进一个槽并执行到期定时器
    current_slot_ = (current_slot_ + 1) % wheel_size_;
    
    Bucket& bucket = wheel_[current_slot_];
    
    // 保存需要重新添加的定时器
    std::vector<std::shared_ptr<TimerEntry>> repeat_entries;
    
    for (auto& entry : bucket) {
        if (entry->cancelled) {
            continue;
        }
        
        // 执行回调
        if (entry->callback) {
            entry->callback();
        }
        
        // 如果是重复定时器，重新添加
        if (entry->repeat && !entry->cancelled) {
            repeat_entries.push_back(entry);
        } else {
            // 从 map 中移除
            timer_map_.erase(entry->id);
        }
    }
    
    // 清空当前槽
    bucket.clear();
    
    // 重新添加重复定时器
    for (auto& entry : repeat_entries) {
        size_t target_slot = (current_slot_ + entry->interval) % wheel_size_;
        wheel_[target_slot].push_back(entry);
    }
}

} // namespace minis3
