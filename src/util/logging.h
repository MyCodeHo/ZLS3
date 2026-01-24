#pragma once

#include <string>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <nlohmann/json.hpp>

namespace minis3 {

/**
 * 日志管理器
 */
class Logger {
public:
    static Logger& Instance();
    
    /**
     * 初始化日志系统
     */
    void Init(const std::string& level,
              const std::string& access_log_path,
              const std::string& error_log_path,
              bool json_format = true);
    
    /**
     * 获取主日志器
     */
    std::shared_ptr<spdlog::logger> GetLogger() { return logger_; }
    
    /**
     * 获取访问日志器
     */
    std::shared_ptr<spdlog::logger> GetAccessLogger() { return access_logger_; }
    
    /**
     * 记录访问日志（结构化 JSON）
     */
    void LogAccess(const std::string& trace_id,
                   const std::string& client_ip,
                   const std::string& method,
                   const std::string& path,
                   int status,
                   int64_t latency_ms,
                   size_t bytes_in,
                   size_t bytes_out,
                   const std::string& bucket = "",
                   const std::string& object = "");

private:
    Logger() = default;
    
    std::shared_ptr<spdlog::logger> logger_;
    std::shared_ptr<spdlog::logger> access_logger_;
    bool json_format_ = true;
};

// 便捷宏
#define LOG_TRACE(...) SPDLOG_LOGGER_TRACE(minis3::Logger::Instance().GetLogger(), __VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(minis3::Logger::Instance().GetLogger(), __VA_ARGS__)
#define LOG_INFO(...) SPDLOG_LOGGER_INFO(minis3::Logger::Instance().GetLogger(), __VA_ARGS__)
#define LOG_WARN(...) SPDLOG_LOGGER_WARN(minis3::Logger::Instance().GetLogger(), __VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_LOGGER_ERROR(minis3::Logger::Instance().GetLogger(), __VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(minis3::Logger::Instance().GetLogger(), __VA_ARGS__)

} // namespace minis3
