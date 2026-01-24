#pragma once

#include "util/config.h"
#include "net/epoll/event_loop.h"
#include "net/epoll/acceptor.h"
#include "net/http/http_router.h"
#include "net/http/http_connection.h"
#include "db/mysql_pool.h"
#include "db/meta_store.h"
#include "storage/data_store.h"
#include "storage/gc.h"
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <mutex>

namespace minis3 {

/**
 * MiniS3 服务器
 * 
 * 负责整体的生命周期管理：
 * - EventLoop 线程池
 * - Acceptor
 * - 连接管理
 * - MySQL 连接池
 * - 存储层
 * - 路由配置
 */
class Server {
public:
    explicit Server(const Config& config);
    ~Server();
    
    // 禁止拷贝
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    
    /**
     * 初始化服务器
     */
    Status Init();
    
    /**
     * 启动服务器（阻塞）
     */
    void Start();
    
    /**
     * 停止服务器
     */
    void Stop();
    
    /**
     * 获取配置
     */
    const Config& GetConfig() const { return config_; }
    
    /**
     * 获取 MetaStore
     */
    MetaStore& GetMetaStore() { return *meta_store_; }
    
    /**
     * 获取 DataStore
     */
    DataStore& GetDataStore() { return *data_store_; }
    
    /**
     * 获取路由器
     */
    HttpRouter& GetRouter() { return router_; }
    
    /**
     * 获取活跃连接数
     */
    size_t ActiveConnections() const;

private:
    void SetupRoutes();
    void OnNewConnection(int sockfd, const std::string& peer_addr);
    void OnConnectionClose(const std::shared_ptr<HttpConnection>& conn);
    void OnRequest(const std::shared_ptr<HttpConnection>& conn, HttpRequest& request);
    void OnBodyData(const std::shared_ptr<HttpConnection>& conn, const char* data, size_t len);
    EventLoop* GetNextLoop();
    bool ShouldStreamUpload(const HttpRequest& request) const;
    
    const Config& config_;
    
    // 主 EventLoop（接受连接）
    std::unique_ptr<EventLoop> main_loop_;
    std::unique_ptr<Acceptor> acceptor_;
    
    // IO 线程池
    std::vector<EventLoop*> io_loops_;
    std::vector<std::thread> io_threads_;
    std::atomic<size_t> next_loop_index_{0};
    std::mutex io_loops_mutex_;
    std::condition_variable io_loops_cv_;
    size_t io_loops_ready_ = 0;
    
    // 连接管理
    mutable std::mutex connections_mutex_;
    std::unordered_set<std::shared_ptr<HttpConnection>> connections_;

    struct UploadContext {
        std::unique_ptr<WriteSession> session;
        std::string expected_sha256;
        size_t bytes_written = 0;
        bool streaming = false;
    };
    mutable std::mutex uploads_mutex_;
    std::unordered_map<int, UploadContext> uploads_;
    
    // 存储层
    std::unique_ptr<MySQLPool> mysql_pool_;
    std::unique_ptr<MetaStore> meta_store_;
    std::unique_ptr<DataStore> data_store_;
    std::unique_ptr<GarbageCollector> gc_;
    
    // 路由
    HttpRouter router_;
    
    // 运行状态
    std::atomic<bool> running_{false};
};

} // namespace minis3
