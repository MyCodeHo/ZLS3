#include "gc.h"
#include "data_store.h"
#include "../db/meta_store.h"
#include "../util/fs.h"
#include "../util/uuid.h"
#include <spdlog/spdlog.h>

namespace minis3 {

GarbageCollector::GarbageCollector(MetaStore& meta_store, DataStore& data_store,
                                   int interval_seconds, int batch_size)
    : meta_store_(meta_store)
    , data_store_(data_store)
    , interval_seconds_(interval_seconds)
    , batch_size_(batch_size)
    , running_(false) {
}

GarbageCollector::~GarbageCollector() {
    Stop();
}

void GarbageCollector::Start() {
    // 启动后台线程
    if (running_) {
        return;
    }
    
    running_ = true;
    gc_thread_ = std::thread(&GarbageCollector::GCLoop, this);
    
    spdlog::info("GC started, interval: {}s, batch_size: {}", 
        interval_seconds_, batch_size_);
}

void GarbageCollector::Stop() {
    // 停止后台线程
    if (!running_) {
        return;
    }
    
    running_ = false;
    cv_.notify_all();
    
    if (gc_thread_.joinable()) {
        gc_thread_.join();
    }
    
    spdlog::info("GC stopped");
}

void GarbageCollector::AddPendingDelete(const std::string& cas_key) {
    // 入队待删除的 CAS key
    std::lock_guard<std::mutex> lock(mutex_);
    pending_deletes_.push(cas_key);
    cv_.notify_one();
}

void GarbageCollector::AddPendingDeletes(const std::vector<std::string>& cas_keys) {
    // 批量入队待删除的 CAS keys
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& key : cas_keys) {
        pending_deletes_.push(key);
    }
    cv_.notify_one();
}

size_t GarbageCollector::QueueLength() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_deletes_.size();
}

void GarbageCollector::RunOnce() {
    ProcessBatch();
}

void GarbageCollector::GCLoop() {
    // 循环等待：超时或有任务就执行批处理
    while (running_) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(interval_seconds_), [this] {
                return !running_ || !pending_deletes_.empty();
            });
        }
        
        if (!running_) {
            break;
        }
        
        ProcessBatch();
    }
}

void GarbageCollector::ProcessBatch() {
    // 从队列取一批任务
    std::vector<std::string> batch;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!pending_deletes_.empty() && 
               static_cast<int>(batch.size()) < batch_size_) {
            batch.push_back(std::move(pending_deletes_.front()));
            pending_deletes_.pop();
        }
    }
    
    if (batch.empty()) {
        return;
    }
    
    spdlog::debug("GC processing {} files", batch.size());
    
    int success_count = 0;
    int fail_count = 0;
    
    // 逐个删除 CAS 文件，并回调更新元数据
    for (const auto& cas_key : batch) {
        auto reclaimable_before = meta_store_.CanDeleteCasBlob(cas_key);
        if (!reclaimable_before.ok()) {
            ++fail_count;
            spdlog::warn("GC failed to check CAS {} before delete: {}",
                         cas_key, reclaimable_before.status().message());
            continue;
        }
        if (!reclaimable_before.value()) {
            spdlog::debug("GC skip {}, CAS is still referenced", cas_key);
            continue;
        }

        bool renamed_to_quarantine = false;
        std::string file_path = data_store_.GetFilePath(cas_key);
        std::string quarantine_path = file_path + ".gc." + UUID::Generate();
        bool source_exists = FileSystem::FileExists(file_path);
        if (source_exists) {
            auto rename_status = FileSystem::AtomicRename(file_path, quarantine_path);
            if (!rename_status.ok()) {
                ++fail_count;
                spdlog::warn("GC failed to quarantine {}: {}", cas_key, rename_status.message());
                continue;
            }
            renamed_to_quarantine = true;
        }

        auto reclaimable_after = meta_store_.CanDeleteCasBlob(cas_key);
        if (!reclaimable_after.ok()) {
            if (renamed_to_quarantine) {
                auto restore_status = FileSystem::AtomicRename(quarantine_path, file_path);
                if (!restore_status.ok()) {
                    spdlog::error("GC failed to restore quarantined file for {}: {}",
                                  cas_key, restore_status.message());
                }
            }
            ++fail_count;
            spdlog::warn("GC failed to re-check CAS {} after quarantine: {}",
                         cas_key, reclaimable_after.status().message());
            continue;
        }

        if (!reclaimable_after.value()) {
            if (renamed_to_quarantine) {
                auto restore_status = FileSystem::AtomicRename(quarantine_path, file_path);
                if (!restore_status.ok()) {
                    ++fail_count;
                    spdlog::error("GC failed to restore CAS {} after race: {}",
                                  cas_key, restore_status.message());
                    continue;
                }
            }
            spdlog::debug("GC cancel delete {}, CAS became referenced again", cas_key);
            continue;
        }

        Status status = Status::OK();
        if (renamed_to_quarantine) {
            status = FileSystem::RemoveFile(quarantine_path);
        }
        bool success = status.ok();
        
        if (success) {
            ++success_count;
        } else {
            ++fail_count;
            spdlog::warn("GC failed to delete {}: {}", cas_key, status.message());
        }
        
        if (delete_callback_) {
            delete_callback_(cas_key, success);
        }
    }
    
    spdlog::info("GC batch completed: {} deleted, {} failed", success_count, fail_count);
}

} // namespace minis3
