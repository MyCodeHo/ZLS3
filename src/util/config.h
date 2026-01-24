#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <yaml-cpp/yaml.h>

namespace minis3 {

/**
 * 服务器配置
 */
struct ServerConfig {
    std::string listen_ip = "0.0.0.0";
    uint16_t listen_port = 8080;
    int io_threads = 4;
    int worker_threads = 8;
    int recv_buffer_size = 0;
    int send_buffer_size = 0;
};

/**
 * 存储配置
 */
struct StorageConfig {
    std::string data_dir = "/var/lib/minis3/data";
    std::string tmp_dir = "/var/lib/minis3/tmp";
};

/**
 * MySQL 配置
 */
struct MySQLConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 3306;
    std::string user = "minis3";
    std::string password = "minis3_password";
    std::string database = "minis3";
    int pool_size = 16;
    int connect_timeout = 5;
    int read_timeout = 30;
};

/**
 * 认证配置
 */
struct AuthConfig {
    bool enabled = true;
    std::vector<std::string> static_tokens;
};

/**
 * 限制配置
 */
struct LimitsConfig {
    size_t max_body_bytes = 5ULL * 1024 * 1024 * 1024;  // 5GB
    size_t max_header_bytes = 8192;
    int max_concurrent_uploads = 100;
    int max_concurrent_downloads = 500;
    int connection_idle_timeout = 60;
    int request_timeout = 3600;
};

/**
 * Multipart 配置
 */
struct MultipartConfig {
    size_t min_part_size = 5 * 1024 * 1024;  // 5MB
    int max_part_number = 10000;
    int expire_hours = 24;
};

/**
 * 日志配置
 */
struct LogConfig {
    std::string level = "info";
    std::string access_log_path = "/var/log/minis3/access.log";
    std::string error_log_path = "/var/log/minis3/error.log";
    bool json_format = true;
};

/**
 * Metrics 配置
 */
struct MetricsConfig {
    bool enabled = true;
    uint16_t listen_port = 9090;
    std::string path = "/metrics";
};

/**
 * GC 配置
 */
struct GCConfig {
    bool enabled = true;
    int interval_seconds = 300;
    int batch_size = 100;
};

/**
 * 总配置
 */
struct Config {
    ServerConfig server;
    StorageConfig storage;
    MySQLConfig mysql;
    AuthConfig auth;
    LimitsConfig limits;
    MultipartConfig multipart;
    LogConfig log;
    MetricsConfig metrics;
    GCConfig gc;
    
    /**
     * 从 YAML 文件加载配置
     */
    static Config LoadFromFile(const std::string& path);
    
    /**
     * 从 YAML 字符串加载配置
     */
    static Config LoadFromString(const std::string& yaml_content);
};

} // namespace minis3
