#pragma once

#include "util/status.h"
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <condition_variable>

namespace minis3 {

class DataStore;
class MetaStore;

/**
 * GC 管理器
 * 
 * 负责清理 ref_count=0 的 CAS 文件
 */
class GarbageCollector {
public:
    using DeleteCallback = std::function<void(const std::string& cas_key, bool success)>;
    
    GarbageCollector(MetaStore& meta_store, DataStore& data_store, 
                     int interval_seconds = 300, int batch_size = 100);
    ~GarbageCollector();
    
    /**
     * 启动 GC
     */
    void Start();
    
    /**
     * 停止 GC
     */
    void Stop();
    
    /**
     * 添加待删除的 CAS key
     */
    void AddPendingDelete(const std::string& cas_key);
    
    /**
     * 批量添加待删除的 CAS keys
     */
    void AddPendingDeletes(const std::vector<std::string>& cas_keys);
    
    /**
     * 设置删除回调（用于通知 MetaStore 更新）
     */
    void SetDeleteCallback(DeleteCallback callback) {
        delete_callback_ = std::move(callback);
    }
    
    /**
     * 获取待处理队列长度
     */
    size_t QueueLength() const;
    
    /**
     * 立即执行一次 GC（用于测试）
     */
    void RunOnce();

private:
    void GCLoop();
    void ProcessBatch();

    MetaStore& meta_store_;
    DataStore& data_store_;
    int interval_seconds_;
    int batch_size_;
    
    std::thread gc_thread_;
    std::atomic<bool> running_;
    
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::string> pending_deletes_;
    
    DeleteCallback delete_callback_;
};

} // namespace minis3
