#include "mysql_pool.h"
#include <spdlog/spdlog.h>

namespace minis3 {

// MySQLConnection 实现

MySQLConnection::MySQLConnection() : mysql_(nullptr) {
}

MySQLConnection::~MySQLConnection() {
    Disconnect();
}

bool MySQLConnection::Connect(const MySQLConfig& config) {
    // 初始化 MySQL 句柄
    mysql_ = mysql_init(nullptr);
    if (!mysql_) {
        spdlog::error("mysql_init failed");
        return false;
    }
    
    // 设置超时
    unsigned int timeout = config.connect_timeout;
    mysql_options(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    
    timeout = config.read_timeout;
    mysql_options(mysql_, MYSQL_OPT_READ_TIMEOUT, &timeout);
    
    // 启用自动重连
    bool reconnect = true;
    mysql_options(mysql_, MYSQL_OPT_RECONNECT, &reconnect);
    
    // 设置字符集
    mysql_options(mysql_, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    
    if (!mysql_real_connect(mysql_,
                            config.host.c_str(),
                            config.user.c_str(),
                            config.password.c_str(),
                            config.database.c_str(),
                            config.port,
                            nullptr,
                            CLIENT_MULTI_STATEMENTS)) {
        spdlog::error("mysql_real_connect failed: {}", mysql_error(mysql_));
        mysql_close(mysql_);
        mysql_ = nullptr;
        return false;
    }
    
    return true;
}

void MySQLConnection::Disconnect() {
    if (mysql_) {
        mysql_close(mysql_);
        mysql_ = nullptr;
    }
}

bool MySQLConnection::IsValid() const {
    return mysql_ != nullptr;
}

bool MySQLConnection::Ping() {
    if (!mysql_) return false;
    return mysql_ping(mysql_) == 0;
}

bool MySQLConnection::Execute(const std::string& sql) {
    // 执行 SQL（无返回结果）
    if (!mysql_) return false;
    
    if (mysql_real_query(mysql_, sql.c_str(), sql.size()) != 0) {
        spdlog::error("MySQL execute error: {} - SQL: {}", mysql_error(mysql_), sql);
        return false;
    }
    
    return true;
}

MYSQL_RES* MySQLConnection::Query(const std::string& sql) {
    // 执行 SQL 并返回结果集
    if (!Execute(sql)) {
        return nullptr;
    }
    
    return mysql_store_result(mysql_);
}

uint64_t MySQLConnection::LastInsertId() const {
    if (!mysql_) return 0;
    return mysql_insert_id(mysql_);
}

uint64_t MySQLConnection::AffectedRows() const {
    if (!mysql_) return 0;
    return mysql_affected_rows(mysql_);
}

std::string MySQLConnection::LastError() const {
    if (!mysql_) return "Connection not established";
    return mysql_error(mysql_);
}

std::string MySQLConnection::Escape(const std::string& str) {
    // 转义字符串防止 SQL 注入
    if (!mysql_) return str;
    
    std::string escaped;
    escaped.resize(str.size() * 2 + 1);
    
    unsigned long len = mysql_real_escape_string(mysql_, escaped.data(), 
        str.c_str(), str.size());
    escaped.resize(len);
    
    return escaped;
}

// MySQLPool 实现

MySQLPool::MySQLPool(const MySQLConfig& config)
    : config_(config)
    , closed_(false)
    , active_count_(0) {
}

MySQLPool::~MySQLPool() {
    Close();
}

bool MySQLPool::Init() {
    // 初始化连接池（预建连接）
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (int i = 0; i < config_.pool_size; ++i) {
        auto conn = std::make_unique<MySQLConnection>();
        if (!conn->Connect(config_)) {
            spdlog::error("Failed to create MySQL connection {}/{}", 
                i + 1, config_.pool_size);
            return false;
        }
        connections_.push(std::move(conn));
    }
    
    spdlog::info("MySQL pool initialized with {} connections", config_.pool_size);
    return true;
}

void MySQLPool::Close() {
    // 关闭连接池并释放连接
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        
        while (!connections_.empty()) {
            connections_.pop();
        }
    }
    
    cv_.notify_all();
    spdlog::info("MySQL pool closed");
}

std::shared_ptr<MySQLConnection> MySQLPool::CreateConnection() {
    // 创建带自动归还的连接
    auto conn = std::make_unique<MySQLConnection>();
    if (!conn->Connect(config_)) {
        return nullptr;
    }
    
    MySQLConnection* raw_ptr = conn.release();
    return std::shared_ptr<MySQLConnection>(raw_ptr, [this](MySQLConnection* c) {
        this->ReturnConnection(c);
    });
}

void MySQLPool::ReturnConnection(MySQLConnection* conn) {
    // 归还连接到池
    if (!conn) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (closed_) {
        delete conn;
        return;
    }
    
    // 检查连接是否有效
    if (conn->Ping()) {
        connections_.push(std::unique_ptr<MySQLConnection>(conn));
        cv_.notify_one();
    } else {
        // 连接已断开，尝试重连
        delete conn;
        auto new_conn = std::make_unique<MySQLConnection>();
        if (new_conn->Connect(config_)) {
            connections_.push(std::move(new_conn));
            cv_.notify_one();
        } else {
            spdlog::warn("Failed to reconnect MySQL, pool size reduced");
        }
    }
    
    --active_count_;
}

std::shared_ptr<MySQLConnection> MySQLPool::GetConnection() {
    return GetConnection(-1);
}

std::shared_ptr<MySQLConnection> MySQLPool::GetConnection(int timeout_ms) {
    // 获取连接（可选超时）
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (closed_) {
        return nullptr;
    }
    
    auto wait_predicate = [this] {
        return closed_ || !connections_.empty();
    };
    
    if (timeout_ms < 0) {
        cv_.wait(lock, wait_predicate);
    } else {
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), wait_predicate)) {
            spdlog::warn("MySQL pool get connection timeout");
            return nullptr;
        }
    }
    
    if (closed_ || connections_.empty()) {
        return nullptr;
    }
    
    auto conn = std::move(connections_.front());
    connections_.pop();
    
    // 检查连接有效性
    if (!conn->Ping()) {
        // 尝试重连
        if (!conn->Connect(config_)) {
            spdlog::error("Failed to reconnect MySQL");
            return nullptr;
        }
    }
    
    ++active_count_;
    
    MySQLConnection* raw_ptr = conn.release();
    return std::shared_ptr<MySQLConnection>(raw_ptr, [this](MySQLConnection* c) {
        this->ReturnConnection(c);
    });
}

size_t MySQLPool::AvailableCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

// PooledConnection 实现

PooledConnection::PooledConnection(MySQLPool& pool)
    : conn_(pool.GetConnection()) {
}

} // namespace minis3
