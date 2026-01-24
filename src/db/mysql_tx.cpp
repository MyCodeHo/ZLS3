#include "mysql_tx.h"
#include "../util/logging.h"

namespace minis3 {

Transaction::Transaction(std::shared_ptr<MySQLConnection> conn)
    : conn_(std::move(conn)), finished_(false) {
    // 开启事务
    if (conn_ && conn_->IsValid()) {
        if (!conn_->Execute("START TRANSACTION")) {
            LOG_ERROR("Failed to start transaction");
        }
    }
}

Transaction::~Transaction() {
    // 未完成则自动回滚
    if (!finished_ && conn_ && conn_->IsValid()) {
        Rollback();
    }
}

bool Transaction::Commit() {
    // 提交事务
    if (finished_) {
        return false;
    }
    finished_ = true;
    if (conn_ && conn_->IsValid()) {
        if (conn_->Execute("COMMIT")) {
            return true;
        }
        LOG_ERROR("Failed to commit transaction");
    }
    return false;
}

void Transaction::Rollback() {
    // 回滚事务
    if (finished_) {
        return;
    }
    finished_ = true;
    if (conn_ && conn_->IsValid()) {
        if (!conn_->Execute("ROLLBACK")) {
            LOG_ERROR("Failed to rollback transaction");
        }
    }
}

bool Transaction::Execute(const std::string& sql) {
    // 执行 SQL（事务内）
    if (finished_ || !conn_) {
        return false;
    }
    return conn_->Execute(sql);
}

MYSQL_RES* Transaction::Query(const std::string& sql) {
    // 查询 SQL（事务内）
    if (finished_ || !conn_) {
        return nullptr;
    }
    return conn_->Query(sql);
}

TransactionScope::TransactionScope(MySQLPool& pool, 
                                   std::function<bool(Transaction&)> action)
    : pool_(pool), action_(std::move(action)) {
}

bool TransactionScope::Execute() {
    // 执行闭包并自动提交/回滚
    auto conn = pool_.GetConnection();
    if (!conn) {
        error_ = "Failed to acquire connection from pool";
        return false;
    }
    
    Transaction tx(conn);
    
    try {
        if (!action_(tx)) {
            error_ = "Transaction action returned false";
            // tx 析构时会自动回滚
            return false;
        }
        
        if (!tx.Commit()) {
            error_ = "Failed to commit transaction";
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        error_ = std::string("Transaction exception: ") + e.what();
        LOG_ERROR("Transaction exception: {}", e.what());
        return false;
    }
}

} // namespace minis3
