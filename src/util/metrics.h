#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <chrono>

namespace minis3 {

/**
 * Prometheus 指标收集器
 */
class Metrics {
public:
    static Metrics& GetInstance();
    
    // 禁止拷贝
    Metrics(const Metrics&) = delete;
    Metrics& operator=(const Metrics&) = delete;
    
    /**
     * 导出 Prometheus 格式的指标
     */
    std::string Export();
    
    // ===== 计数器 =====
    
    /**
     * 增加 HTTP 请求计数
     */
    void IncrementHttpRequests(const std::string& method, int status_code);
    
    /**
     * 增加上传字节数
     */
    void AddUploadBytes(size_t bytes);
    
    /**
     * 增加下载字节数
     */
    void AddDownloadBytes(size_t bytes);
    
    // ===== 计量器 =====
    
    /**
     * 设置活跃连接数
     */
    void SetActiveConnections(int count);
    
    /**
     * 增加活跃连接数
     */
    void IncrementActiveConnections();
    
    /**
     * 减少活跃连接数
     */
    void DecrementActiveConnections();
    
    /**
     * 设置存储使用字节数
     */
    void SetStorageUsedBytes(uint64_t bytes);
    
    /**
     * 设置对象数量
     */
    void SetObjectsCount(uint64_t count);
    
    /**
     * 设置 CAS blob 数量
     */
    void SetCasBlobsCount(uint64_t count);
    
    /**
     * 设置 GC 队列长度
     */
    void SetGcQueueLength(uint64_t length);
    
    // ===== 直方图 =====
    
    /**
     * 记录请求延迟
     */
    void ObserveRequestDuration(const std::string& method, const std::string& route, double seconds);

private:
    Metrics() = default;
    
    // HTTP 请求计数 {method_status -> count}
    std::mutex http_requests_mutex_;
    std::unordered_map<std::string, uint64_t> http_requests_;
    
    // 字节计数
    std::atomic<uint64_t> upload_bytes_{0};
    std::atomic<uint64_t> download_bytes_{0};
    
    // 计量器
    std::atomic<int> active_connections_{0};
    std::atomic<uint64_t> storage_used_bytes_{0};
    std::atomic<uint64_t> objects_count_{0};
    std::atomic<uint64_t> cas_blobs_count_{0};
    std::atomic<uint64_t> gc_queue_length_{0};
    
    // 请求延迟直方图 bucket
    std::mutex duration_mutex_;
    std::unordered_map<std::string, std::vector<double>> request_durations_;
    
    // 直方图 bucket 边界
    static constexpr double kBuckets[] = {
        0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0
    };
};

/**
 * 日志辅助类
 */
class Logging {
public:
    /**
     * 初始化日志系统
     */
    static void Init(const std::string& level,
                     const std::string& access_log_path,
                     const std::string& error_log_path,
                     bool json_format);
    
    /**
     * 记录访问日志
     */
    static void AccessLog(const class HttpRequest& request,
                         const class HttpResponse& response,
                         int64_t latency_ms);
};

} // namespace minis3
