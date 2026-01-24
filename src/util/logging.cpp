#include "logging.h"
#include <filesystem>
#include <chrono>
#include <spdlog/sinks/basic_file_sink.h>

namespace minis3 {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Init(const std::string& level,
                  const std::string& access_log_path,
                  const std::string& error_log_path,
                  bool json_format) {
    // 初始化日志器与输出位置
    json_format_ = json_format;
    
    // 创建日志目录
    if (!error_log_path.empty()) {
        std::filesystem::create_directories(
            std::filesystem::path(error_log_path).parent_path());
    }
    if (!access_log_path.empty()) {
        std::filesystem::create_directories(
            std::filesystem::path(access_log_path).parent_path());
    }
    
    // 创建主日志器
    std::vector<spdlog::sink_ptr> sinks;
    
    // 控制台输出
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    sinks.push_back(console_sink);
    
    // 文件输出
    if (!error_log_path.empty()) {
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            error_log_path, 100 * 1024 * 1024, 10);  // 100MB, 保留10个
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
        sinks.push_back(file_sink);
    }
    
    logger_ = std::make_shared<spdlog::logger>("minis3", sinks.begin(), sinks.end());
    
    // 设置日志级别
    if (level == "trace") {
        logger_->set_level(spdlog::level::trace);
    } else if (level == "debug") {
        logger_->set_level(spdlog::level::debug);
    } else if (level == "info") {
        logger_->set_level(spdlog::level::info);
    } else if (level == "warn") {
        logger_->set_level(spdlog::level::warn);
    } else if (level == "error") {
        logger_->set_level(spdlog::level::err);
    } else {
        logger_->set_level(spdlog::level::info);
    }
    
    // 设置为默认日志器
    spdlog::set_default_logger(logger_);
    
    // 创建访问日志器
    if (!access_log_path.empty()) {
        auto access_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            access_log_path, 100 * 1024 * 1024, 10);
        access_sink->set_pattern("%v");  // 只输出消息内容
        access_logger_ = std::make_shared<spdlog::logger>("access", access_sink);
        access_logger_->set_level(spdlog::level::info);
    } else {
        // 如果没有配置访问日志文件，使用控制台
        access_logger_ = std::make_shared<spdlog::logger>("access", console_sink);
        access_logger_->set_level(spdlog::level::info);
    }
    
    logger_->info("Logger initialized, level: {}", level);
}

void Logger::LogAccess(const std::string& trace_id,
                       const std::string& client_ip,
                       const std::string& method,
                       const std::string& path,
                       int status,
                       int64_t latency_ms,
                       size_t bytes_in,
                       size_t bytes_out,
                       const std::string& bucket,
                       const std::string& object) {
    // 输出访问日志（支持 JSON 或普通格式）
    if (!access_logger_) return;
    
    if (json_format_) {
        // JSON 格式
        nlohmann::json log_entry = {
            {"ts", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()},
            {"trace_id", trace_id},
            {"client_ip", client_ip},
            {"method", method},
            {"path", path},
            {"status", status},
            {"latency_ms", latency_ms},
            {"bytes_in", bytes_in},
            {"bytes_out", bytes_out}
        };
        
        if (!bucket.empty()) {
            log_entry["bucket"] = bucket;
        }
        if (!object.empty()) {
            log_entry["object"] = object;
        }
        
        access_logger_->info(log_entry.dump());
    } else {
        // 普通格式
        access_logger_->info("{} {} {} {} {} {}ms {} {}",
            trace_id, client_ip, method, path, status,
            latency_ms, bytes_in, bytes_out);
    }
}

} // namespace minis3
