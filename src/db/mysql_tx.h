#pragma once

#include "mysql_pool.h"
#include <functional>

namespace minis3 {

/**
 * RAII 事务封装
 */
class Transaction {
public:
    explicit Transaction(std::shared_ptr<MySQLConnection> conn);
    ~Transaction();
    
    // 禁止拷贝
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    
    /**
     * 提交事务
     */
    bool Commit();
    
    /**
     * 回滚事务
     */
    void Rollback();
    
    /**
     * 获取连接
     */
    MySQLConnection* Connection() { return conn_.get(); }
    
    /**
     * 事务是否已结束
     */
    bool IsFinished() const { return finished_; }
    
    /**
     * 执行 SQL
     */
    bool Execute(const std::string& sql);
    
    /**
     * 执行查询
     */
    MYSQL_RES* Query(const std::string& sql);

private:
    std::shared_ptr<MySQLConnection> conn_;
    bool finished_;
};

/**
 * 事务作用域
 */
class TransactionScope {
public:
    TransactionScope(MySQLPool& pool, std::function<bool(Transaction&)> action);
    
    bool Execute();
    
    const std::string& Error() const { return error_; }

private:
    MySQLPool& pool_;
    std::function<bool(Transaction&)> action_;
    std::string error_;
};

} // namespace minis3
