#include "config.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace minis3 {

namespace {

template<typename T>
T GetOrDefault(const YAML::Node& node, const std::string& key, const T& default_value) {
    // 从节点读取字段，缺省则返回默认值
    if (node[key]) {
        return node[key].as<T>();
    }
    return default_value;
}

ServerConfig ParseServerConfig(const YAML::Node& node) {
    // 解析 server 配置
    ServerConfig config;
    if (!node) return config;
    
    config.listen_ip = GetOrDefault<std::string>(node, "listen_ip", config.listen_ip);
    config.listen_port = GetOrDefault<uint16_t>(node, "listen_port", config.listen_port);
    config.io_threads = GetOrDefault<int>(node, "io_threads", config.io_threads);
    config.worker_threads = GetOrDefault<int>(node, "worker_threads", config.worker_threads);
    config.recv_buffer_size = GetOrDefault<int>(node, "recv_buffer_size", config.recv_buffer_size);
    config.send_buffer_size = GetOrDefault<int>(node, "send_buffer_size", config.send_buffer_size);
    
    return config;
}

StorageConfig ParseStorageConfig(const YAML::Node& node) {
    // 解析 storage 配置
    StorageConfig config;
    if (!node) return config;
    
    config.data_dir = GetOrDefault<std::string>(node, "data_dir", config.data_dir);
    config.tmp_dir = GetOrDefault<std::string>(node, "tmp_dir", config.tmp_dir);
    
    return config;
}

MySQLConfig ParseMySQLConfig(const YAML::Node& node) {
    // 解析 mysql 配置
    MySQLConfig config;
    if (!node) return config;
    
    config.host = GetOrDefault<std::string>(node, "host", config.host);
    config.port = GetOrDefault<uint16_t>(node, "port", config.port);
    config.user = GetOrDefault<std::string>(node, "user", config.user);
    config.password = GetOrDefault<std::string>(node, "password", config.password);
    config.database = GetOrDefault<std::string>(node, "database", config.database);
    config.pool_size = GetOrDefault<int>(node, "pool_size", config.pool_size);
    config.connect_timeout = GetOrDefault<int>(node, "connect_timeout", config.connect_timeout);
    config.read_timeout = GetOrDefault<int>(node, "read_timeout", config.read_timeout);
    
    return config;
}

AuthConfig ParseAuthConfig(const YAML::Node& node) {
    // 解析 auth 配置
    AuthConfig config;
    if (!node) return config;
    
    config.enabled = GetOrDefault<bool>(node, "enabled", config.enabled);
    
    if (node["static_tokens"]) {
        for (const auto& token : node["static_tokens"]) {
            config.static_tokens.push_back(token.as<std::string>());
        }
    }
    
    return config;
}

LimitsConfig ParseLimitsConfig(const YAML::Node& node) {
    // 解析 limits 配置
    LimitsConfig config;
    if (!node) return config;
    
    config.max_body_bytes = GetOrDefault<size_t>(node, "max_body_bytes", config.max_body_bytes);
    config.max_header_bytes = GetOrDefault<size_t>(node, "max_header_bytes", config.max_header_bytes);
    config.max_concurrent_uploads = GetOrDefault<int>(node, "max_concurrent_uploads", config.max_concurrent_uploads);
    config.max_concurrent_downloads = GetOrDefault<int>(node, "max_concurrent_downloads", config.max_concurrent_downloads);
    config.connection_idle_timeout = GetOrDefault<int>(node, "connection_idle_timeout", config.connection_idle_timeout);
    config.request_timeout = GetOrDefault<int>(node, "request_timeout", config.request_timeout);
    
    return config;
}

MultipartConfig ParseMultipartConfig(const YAML::Node& node) {
    // 解析 multipart 配置
    MultipartConfig config;
    if (!node) return config;
    
    config.min_part_size = GetOrDefault<size_t>(node, "min_part_size", config.min_part_size);
    config.max_part_number = GetOrDefault<int>(node, "max_part_number", config.max_part_number);
    config.expire_hours = GetOrDefault<int>(node, "expire_hours", config.expire_hours);
    
    return config;
}

LogConfig ParseLogConfig(const YAML::Node& node) {
    // 解析 log 配置
    LogConfig config;
    if (!node) return config;
    
    config.level = GetOrDefault<std::string>(node, "level", config.level);
    config.access_log_path = GetOrDefault<std::string>(node, "access_log_path", config.access_log_path);
    config.error_log_path = GetOrDefault<std::string>(node, "error_log_path", config.error_log_path);
    config.json_format = GetOrDefault<bool>(node, "json_format", config.json_format);
    
    return config;
}

MetricsConfig ParseMetricsConfig(const YAML::Node& node) {
    // 解析 metrics 配置
    MetricsConfig config;
    if (!node) return config;
    
    config.enabled = GetOrDefault<bool>(node, "enabled", config.enabled);
    config.listen_port = GetOrDefault<uint16_t>(node, "listen_port", config.listen_port);
    config.path = GetOrDefault<std::string>(node, "path", config.path);
    
    return config;
}

GCConfig ParseGCConfig(const YAML::Node& node) {
    // 解析 gc 配置
    GCConfig config;
    if (!node) return config;
    
    config.enabled = GetOrDefault<bool>(node, "enabled", config.enabled);
    config.interval_seconds = GetOrDefault<int>(node, "interval_seconds", config.interval_seconds);
    config.batch_size = GetOrDefault<int>(node, "batch_size", config.batch_size);
    
    return config;
}

} // anonymous namespace

Config Config::LoadFromFile(const std::string& path) {
    // 从文件加载 YAML 配置
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + path);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return LoadFromString(buffer.str());
}

Config Config::LoadFromString(const std::string& yaml_content) {
    // 从字符串解析 YAML 配置
    YAML::Node root = YAML::Load(yaml_content);
    
    Config config;
    config.server = ParseServerConfig(root["server"]);
    config.storage = ParseStorageConfig(root["storage"]);
    config.mysql = ParseMySQLConfig(root["mysql"]);
    config.auth = ParseAuthConfig(root["auth"]);
    config.limits = ParseLimitsConfig(root["limits"]);
    config.multipart = ParseMultipartConfig(root["multipart"]);
    config.log = ParseLogConfig(root["log"]);
    config.metrics = ParseMetricsConfig(root["metrics"]);
    config.gc = ParseGCConfig(root["gc"]);
    
    return config;
}

} // namespace minis3
