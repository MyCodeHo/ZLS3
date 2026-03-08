#pragma once

#include "config.h"
#include "status.h"
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace minis3 {

class RedisCache {
public:
    explicit RedisCache(const RedisConfig& config);
    ~RedisCache();

    Status Init();
    bool Enabled() const { return config_.enabled; }

    Result<std::optional<std::string>> Get(const std::string& key);
    Status SetEx(const std::string& key, int ttl_seconds, const std::string& value);
    Status Del(const std::string& key);

    int ObjectMetaTtlSeconds() const { return config_.object_meta_ttl_seconds; }

private:
    Status Connect();
    void Close();
    Status EnsureConnected();

    Result<std::optional<std::string>> SendCommandGetBulk(const std::vector<std::string>& args);
    Status SendCommandExpectOk(const std::vector<std::string>& args);

    bool SendAll(const std::string& data);
    bool ReadLine(std::string& line);
    bool ReadExact(size_t len, std::string& out);

    std::string BuildRespCommand(const std::vector<std::string>& args) const;

    RedisConfig config_;
    int sockfd_ = -1;
    std::mutex mutex_;
};

} // namespace minis3
