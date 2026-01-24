#include "metrics.h"
#include "logging.h"
#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace minis3 {

Metrics& Metrics::GetInstance() {
    static Metrics instance;
    return instance;
}

std::string Metrics::Export() {
    // 导出 Prometheus 文本格式指标
    std::ostringstream ss;
    
    // HTTP 请求计数
    {
        std::lock_guard<std::mutex> lock(http_requests_mutex_);
        ss << "# HELP minis3_http_requests_total Total number of HTTP requests\n";
        ss << "# TYPE minis3_http_requests_total counter\n";
        for (const auto& [key, count] : http_requests_) {
            // key 格式: "method_status"
            size_t pos = key.find('_');
            if (pos != std::string::npos) {
                std::string method = key.substr(0, pos);
                std::string code = key.substr(pos + 1);
                ss << "minis3_http_requests_total{method=\"" << method 
                   << "\",code=\"" << code << "\"} " << count << "\n";
            }
        }
    }
    
    // 上传字节数
    ss << "# HELP minis3_upload_bytes_total Total bytes uploaded\n";
    ss << "# TYPE minis3_upload_bytes_total counter\n";
    ss << "minis3_upload_bytes_total " << upload_bytes_.load() << "\n";
    
    // 下载字节数
    ss << "# HELP minis3_download_bytes_total Total bytes downloaded\n";
    ss << "# TYPE minis3_download_bytes_total counter\n";
    ss << "minis3_download_bytes_total " << download_bytes_.load() << "\n";
    
    // 活跃连接数
    ss << "# HELP minis3_active_connections Current number of active connections\n";
    ss << "# TYPE minis3_active_connections gauge\n";
    ss << "minis3_active_connections " << active_connections_.load() << "\n";
    
    // 存储使用
    ss << "# HELP minis3_storage_used_bytes Total storage used in bytes\n";
    ss << "# TYPE minis3_storage_used_bytes gauge\n";
    ss << "minis3_storage_used_bytes " << storage_used_bytes_.load() << "\n";
    
    // 对象数量
    ss << "# HELP minis3_objects_count Total number of objects\n";
    ss << "# TYPE minis3_objects_count gauge\n";
    ss << "minis3_objects_count " << objects_count_.load() << "\n";
    
    // CAS blob 数量
    ss << "# HELP minis3_cas_blobs_count Total number of CAS blobs\n";
    ss << "# TYPE minis3_cas_blobs_count gauge\n";
    ss << "minis3_cas_blobs_count " << cas_blobs_count_.load() << "\n";
    
    // GC 队列长度
    ss << "# HELP minis3_gc_queue_length Current GC queue length\n";
    ss << "# TYPE minis3_gc_queue_length gauge\n";
    ss << "minis3_gc_queue_length " << gc_queue_length_.load() << "\n";
    
    // 请求延迟直方图
    {
        std::lock_guard<std::mutex> lock(duration_mutex_);
        ss << "# HELP minis3_http_request_duration_seconds HTTP request duration in seconds\n";
        ss << "# TYPE minis3_http_request_duration_seconds histogram\n";
        
        for (const auto& [key, durations] : request_durations_) {
            if (durations.empty()) continue;
            
            // key 格式: "method_route"
            size_t pos = key.find('_');
            std::string method = (pos != std::string::npos) ? key.substr(0, pos) : key;
            std::string route = (pos != std::string::npos) ? key.substr(pos + 1) : "";
            
            // 计算直方图 bucket
            std::vector<size_t> bucket_counts(sizeof(kBuckets) / sizeof(kBuckets[0]), 0);
            double sum = 0;
            
            for (double d : durations) {
                sum += d;
                for (size_t i = 0; i < bucket_counts.size(); ++i) {
                    if (d <= kBuckets[i]) {
                        ++bucket_counts[i];
                    }
                }
            }
            
            // 输出 bucket
            for (size_t i = 0; i < bucket_counts.size(); ++i) {
                ss << "minis3_http_request_duration_seconds_bucket{method=\"" << method
                   << "\",route=\"" << route << "\",le=\"" << kBuckets[i] << "\"} "
                   << bucket_counts[i] << "\n";
            }
            ss << "minis3_http_request_duration_seconds_bucket{method=\"" << method
               << "\",route=\"" << route << "\",le=\"+Inf\"} " << durations.size() << "\n";
            
            ss << "minis3_http_request_duration_seconds_sum{method=\"" << method
               << "\",route=\"" << route << "\"} " << std::fixed << std::setprecision(6) << sum << "\n";
            ss << "minis3_http_request_duration_seconds_count{method=\"" << method
               << "\",route=\"" << route << "\"} " << durations.size() << "\n";
        }
    }
    
    return ss.str();
}

void Metrics::IncrementHttpRequests(const std::string& method, int status_code) {
    // 递增请求计数
    std::string key = method + "_" + std::to_string(status_code);
    std::lock_guard<std::mutex> lock(http_requests_mutex_);
    ++http_requests_[key];
}

void Metrics::AddUploadBytes(size_t bytes) {
    upload_bytes_.fetch_add(bytes);
}

void Metrics::AddDownloadBytes(size_t bytes) {
    download_bytes_.fetch_add(bytes);
}

void Metrics::SetActiveConnections(int count) {
    active_connections_.store(count);
}

void Metrics::IncrementActiveConnections() {
    active_connections_.fetch_add(1);
}

void Metrics::DecrementActiveConnections() {
    active_connections_.fetch_sub(1);
}

void Metrics::SetStorageUsedBytes(uint64_t bytes) {
    storage_used_bytes_.store(bytes);
}

void Metrics::SetObjectsCount(uint64_t count) {
    objects_count_.store(count);
}

void Metrics::SetCasBlobsCount(uint64_t count) {
    cas_blobs_count_.store(count);
}

void Metrics::SetGcQueueLength(uint64_t length) {
    gc_queue_length_.store(length);
}

void Metrics::ObserveRequestDuration(const std::string& method, const std::string& route, double seconds) {
    // 记录请求耗时样本
    std::string key = method + "_" + route;
    std::lock_guard<std::mutex> lock(duration_mutex_);
    request_durations_[key].push_back(seconds);
    
    // 限制保存的样本数量（避免内存增长）
    if (request_durations_[key].size() > 10000) {
        request_durations_[key].erase(
            request_durations_[key].begin(),
            request_durations_[key].begin() + 5000);
    }
}

// ===== Logging =====

void Logging::Init(const std::string& level,
                   const std::string& access_log_path,
                   const std::string& error_log_path,
                   bool json_format) {
    // 初始化日志系统
    Logger::Instance().Init(level, access_log_path, error_log_path, json_format);
}

void Logging::AccessLog(const HttpRequest& request,
                       const HttpResponse& response,
                       int64_t latency_ms) {
    // 记录指标
    Metrics::GetInstance().IncrementHttpRequests(
        HttpMethodToString(request.Method()),
        static_cast<int>(response.StatusCode()));
    
    Metrics::GetInstance().ObserveRequestDuration(
        HttpMethodToString(request.Method()),
        request.Path(),
        latency_ms / 1000.0);
    
    size_t bytes_in = request.Body().size();
    std::string internal_size = request.GetHeader("X-Internal-Size");
    if (!internal_size.empty()) {
        try {
            bytes_in = static_cast<size_t>(std::stoull(internal_size));
        } catch (...) {
        }
    }

    size_t bytes_out = response.HasFile() ? response.FileLength() : response.Body().size();

    if (request.Method() == HttpMethod::PUT || request.Method() == HttpMethod::POST) {
        Metrics::GetInstance().AddUploadBytes(bytes_in);
    }
    if (request.Method() == HttpMethod::GET) {
        Metrics::GetInstance().AddDownloadBytes(bytes_out);
    }

    // 记录访问日志
    Logger::Instance().LogAccess(
        request.TraceId(),
        request.ClientIp(),
        HttpMethodToString(request.Method()),
        request.Path(),
        static_cast<int>(response.StatusCode()),
        latency_ms,
        bytes_in,
        bytes_out);
}

} // namespace minis3
