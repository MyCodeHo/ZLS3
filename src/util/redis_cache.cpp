#include "redis_cache.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace minis3 {

RedisCache::RedisCache(const RedisConfig& config)
    : config_(config) {}

RedisCache::~RedisCache() {
    Close();
}

Status RedisCache::Init() {
    if (!config_.enabled) {
        return Status::OK();
    }
    return Connect();
}

Status RedisCache::Connect() {
    Close();

    sockfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ < 0) {
        return Status::Error(ErrorCode::InternalError, "Failed to create Redis socket");
    }

    struct timeval timeout;
    timeout.tv_sec = config_.command_timeout_ms / 1000;
    timeout.tv_usec = (config_.command_timeout_ms % 1000) * 1000;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(sockfd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    int nodelay = 1;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.port);
    if (::inet_pton(AF_INET, config_.host.c_str(), &addr.sin_addr) != 1) {
        Close();
        return Status::Error(ErrorCode::InvalidArgument, "Invalid Redis host");
    }

    if (::connect(sockfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        Close();
        return Status::Error(ErrorCode::ServiceUnavailable, "Failed to connect Redis");
    }

    if (!config_.password.empty()) {
        auto status = SendCommandExpectOk({"AUTH", config_.password});
        if (!status.ok()) {
            Close();
            return status;
        }
    }

    if (config_.db > 0) {
        auto status = SendCommandExpectOk({"SELECT", std::to_string(config_.db)});
        if (!status.ok()) {
            Close();
            return status;
        }
    }

    return SendCommandExpectOk({"PING"});
}

void RedisCache::Close() {
    if (sockfd_ >= 0) {
        ::close(sockfd_);
        sockfd_ = -1;
    }
}

Status RedisCache::EnsureConnected() {
    if (sockfd_ >= 0) {
        return Status::OK();
    }
    return Connect();
}

Result<std::optional<std::string>> RedisCache::Get(const std::string& key) {
    if (!config_.enabled) {
        return Result<std::optional<std::string>>::Ok(std::nullopt);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto status = EnsureConnected();
    if (!status.ok()) {
        return Result<std::optional<std::string>>::Err(status);
    }

    auto res = SendCommandGetBulk({"GET", key});
    if (!res.ok()) {
        Close();
        return res;
    }
    return res;
}

Status RedisCache::SetEx(const std::string& key, int ttl_seconds, const std::string& value) {
    if (!config_.enabled) {
        return Status::OK();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto status = EnsureConnected();
    if (!status.ok()) {
        return status;
    }

    status = SendCommandExpectOk({"SET", key, value, "EX", std::to_string(ttl_seconds)});
    if (!status.ok()) {
        Close();
    }
    return status;
}

Status RedisCache::Del(const std::string& key) {
    if (!config_.enabled) {
        return Status::OK();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto status = EnsureConnected();
    if (!status.ok()) {
        return status;
    }

    status = SendCommandExpectOk({"DEL", key});
    if (!status.ok()) {
        Close();
    }
    return status;
}

Result<std::optional<std::string>> RedisCache::SendCommandGetBulk(const std::vector<std::string>& args) {
    std::string cmd = BuildRespCommand(args);
    if (!SendAll(cmd)) {
        return Result<std::optional<std::string>>::Err(ErrorCode::ServiceUnavailable, "Redis write failed");
    }

    std::string line;
    if (!ReadLine(line)) {
        return Result<std::optional<std::string>>::Err(ErrorCode::ServiceUnavailable, "Redis read failed");
    }

    if (line.empty()) {
        return Result<std::optional<std::string>>::Err(ErrorCode::ServiceUnavailable, "Redis empty response");
    }

    char type = line[0];
    if (type == '$') {
        int len = std::stoi(line.substr(1));
        if (len < 0) {
            return Result<std::optional<std::string>>::Ok(std::nullopt);
        }
        std::string payload;
        if (!ReadExact(static_cast<size_t>(len), payload)) {
            return Result<std::optional<std::string>>::Err(ErrorCode::ServiceUnavailable, "Redis bulk read failed");
        }
        std::string crlf;
        if (!ReadExact(2, crlf)) {
            return Result<std::optional<std::string>>::Err(ErrorCode::ServiceUnavailable, "Redis trailing CRLF missing");
        }
        return Result<std::optional<std::string>>::Ok(payload);
    }

    if (type == '-') {
        return Result<std::optional<std::string>>::Err(ErrorCode::ServiceUnavailable, "Redis error: " + line.substr(1));
    }

    return Result<std::optional<std::string>>::Err(ErrorCode::ServiceUnavailable, "Unexpected Redis response type");
}

Status RedisCache::SendCommandExpectOk(const std::vector<std::string>& args) {
    std::string cmd = BuildRespCommand(args);
    if (!SendAll(cmd)) {
        return Status::Error(ErrorCode::ServiceUnavailable, "Redis write failed");
    }

    std::string line;
    if (!ReadLine(line)) {
        return Status::Error(ErrorCode::ServiceUnavailable, "Redis read failed");
    }

    if (line.empty()) {
        return Status::Error(ErrorCode::ServiceUnavailable, "Redis empty response");
    }

    char type = line[0];
    if (type == '+' || type == ':' || type == '$') {
        return Status::OK();
    }
    if (type == '-') {
        return Status::Error(ErrorCode::ServiceUnavailable, "Redis error: " + line.substr(1));
    }
    return Status::Error(ErrorCode::ServiceUnavailable, "Unexpected Redis response type");
}

bool RedisCache::SendAll(const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(sockfd_, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool RedisCache::ReadLine(std::string& line) {
    line.clear();
    char ch = 0;
    char prev = 0;
    while (true) {
        ssize_t n = ::recv(sockfd_, &ch, 1, 0);
        if (n <= 0) {
            return false;
        }
        line.push_back(ch);
        if (prev == '\r' && ch == '\n') {
            line.resize(line.size() - 2);
            return true;
        }
        prev = ch;
    }
}

bool RedisCache::ReadExact(size_t len, std::string& out) {
    out.clear();
    out.resize(len);
    size_t readn = 0;
    while (readn < len) {
        ssize_t n = ::recv(sockfd_, out.data() + readn, len - readn, 0);
        if (n <= 0) {
            return false;
        }
        readn += static_cast<size_t>(n);
    }
    return true;
}

std::string RedisCache::BuildRespCommand(const std::vector<std::string>& args) const {
    std::string cmd = "*" + std::to_string(args.size()) + "\r\n";
    for (const auto& arg : args) {
        cmd += "$" + std::to_string(arg.size()) + "\r\n";
        cmd += arg + "\r\n";
    }
    return cmd;
}

} // namespace minis3
