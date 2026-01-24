#pragma once

#include "util/config.h"
#include <mysql/mysql.h>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace minis3 {

/**
 * MySQL 连接包装
 */
class MySQLConnection {
public:
    MySQLConnection();
    ~MySQLConnection();
    
    // 禁止拷贝
    MySQLConnection(const MySQLConnection&) = delete;
    MySQLConnection& operator=(const MySQLConnection&) = delete;
    
    /**
     * 连接到 MySQL
     */
    bool Connect(const MySQLConfig& config);
    
    /**
     * 断开连接
     */
    void Disconnect();
    
    /**
     * 检查连接是否有效
     */
    bool IsValid() const;
    
    /**
     * Ping 检查连接
     */
    bool Ping();
    
    /**
     * 获取原始 MYSQL 句柄
     */
    MYSQL* Handle() { return mysql_; }
    
    /**
     * 执行查询
     */
    bool Execute(const std::string& sql);
    
    /**
     * 执行查询并获取结果
     */
    MYSQL_RES* Query(const std::string& sql);
    
    /**
     * 获取最后插入的 ID
     */
    uint64_t LastInsertId() const;
    
    /**
     * 获取受影响的行数
     */
    uint64_t AffectedRows() const;
    
    /**
     * 获取最后的错误信息
     */
    std::string LastError() const;
    
    /**
     * 转义字符串
     */
    std::string Escape(const std::string& str);

private:
    MYSQL* mysql_;
};

/**
 * MySQL 连接池
 */
class MySQLPool {
public:
    explicit MySQLPool(const MySQLConfig& config);
    ~MySQLPool();
    
    /**
     * 初始化连接池
     */
    bool Init();
    
    /**
     * 关闭连接池
     */
    void Close();
    
    /**
     * 获取连接（阻塞等待）
     */
    std::shared_ptr<MySQLConnection> GetConnection();
    
    /**
     * 获取连接（带超时）
     * @param timeout_ms 超时时间（毫秒）
     */
    std::shared_ptr<MySQLConnection> GetConnection(int timeout_ms);
    
    /**
     * 获取当前可用连接数
     */
    size_t AvailableCount() const;
    
    /**
     * 获取总连接数
     */
    size_t TotalCount() const { return config_.pool_size; }

private:
    void ReturnConnection(MySQLConnection* conn);
    std::shared_ptr<MySQLConnection> CreateConnection();

    MySQLConfig config_;
    std::queue<std::unique_ptr<MySQLConnection>> connections_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> closed_;
    std::atomic<int> active_count_;
};

/**
 * 连接池连接的 RAII 包装
 */
class PooledConnection {
public:
    PooledConnection(MySQLPool& pool);
    ~PooledConnection() = default;
    
    // 禁止拷贝
    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;
    
    // 允许移动
    PooledConnection(PooledConnection&&) = default;
    PooledConnection& operator=(PooledConnection&&) = default;
    
    MySQLConnection* operator->() { return conn_.get(); }
    MySQLConnection& operator*() { return *conn_; }
    
    bool IsValid() const { return conn_ != nullptr && conn_->IsValid(); }
    explicit operator bool() const { return IsValid(); }

private:
    std::shared_ptr<MySQLConnection> conn_;
};

} // namespace minis3
