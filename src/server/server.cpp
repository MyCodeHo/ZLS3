#include "server.h"
#include "api/handlers/health_handlers.h"
#include "api/handlers/bucket_handlers.h"
#include "api/handlers/object_handlers.h"
#include "api/handlers/multipart_handlers.h"
#include "api/handlers/presign_handlers.h"
#include "api/middleware/auth_middleware.h"
#include "api/middleware/trace_middleware.h"
#include "util/logging.h"
#include "util/metrics.h"
#include <spdlog/spdlog.h>
#include <signal.h>

namespace minis3 {

Server::Server(const Config& config)
    : config_(config) {
}

Server::~Server() {
    Stop();
}

Status Server::Init() {
    spdlog::info("Initializing MiniS3 server...");
    
    // 1. 初始化日志（访问日志/错误日志/JSON 格式等）
    Logging::Init(config_.log.level,
                  config_.log.access_log_path,
                  config_.log.error_log_path,
                  config_.log.json_format);
    
    // 2. 初始化 MySQL 连接池（元数据读写的基础设施）
    spdlog::info("Connecting to MySQL {}:{}...", config_.mysql.host, config_.mysql.port);
    mysql_pool_ = std::make_unique<MySQLPool>(config_.mysql);
    
    auto pool_status = mysql_pool_->Init();
    if (!pool_status) {
        return Status::Error(ErrorCode::DatabaseError, "Failed to initialize MySQL pool");
    }
    
    // 3. 初始化 MetaStore（SQL 访问与事务封装）
    meta_store_ = std::make_unique<MetaStore>(*mysql_pool_);
    
    // 4. 初始化 DataStore（CAS 文件存储）
    spdlog::info("Initializing storage at {}...", config_.storage.data_dir);
    data_store_ = std::make_unique<DataStore>(
        config_.storage.data_dir,
        config_.storage.tmp_dir
    );
    
    auto store_status = data_store_->Init();
    if (!store_status.ok()) {
        return store_status;
    }
    
    // 5. 初始化 GC（定期清理 ref_count=0 的 CAS 文件）
    if (config_.gc.enabled) {
        gc_ = std::make_unique<GarbageCollector>(
            *meta_store_,
            *data_store_,
            config_.gc.interval_seconds,
            config_.gc.batch_size
        );
        // 删除成功后同步清理元数据记录
        gc_->SetDeleteCallback([this](const std::string& cas_key, bool success) {
            if (success && meta_store_) {
                meta_store_->DeleteCasBlob(cas_key);
            }
        });
    }
    
    // 6. 创建主 EventLoop（仅负责 accept 与分发）
    main_loop_ = std::make_unique<EventLoop>();
    
    // 7. 创建 Acceptor（监听端口并接收连接）
    acceptor_ = std::make_unique<Acceptor>(
        main_loop_.get(),
        config_.server.listen_ip,
        config_.server.listen_port
    );
    
    acceptor_->SetNewConnectionCallback(
        [this](int sockfd, const std::string& peer_addr) {
            OnNewConnection(sockfd, peer_addr);
        }
    );
    
    // 8. 设置路由（注册中间件与各 API handler）
    SetupRoutes();
    
    spdlog::info("Server initialized successfully");
    return Status::OK();
}

void Server::SetupRoutes() {
    // 添加中间件：Trace 在前，Auth 可选
    router_.Use(TraceMiddleware::Handle);
    
    if (config_.auth.enabled) {
        router_.Use([this](HttpRequest& req, HttpHandler next) {
            return AuthMiddleware::Handle(req, next, config_.auth.static_tokens);
        });
    }
    
    // 健康检查（不需要认证）
    router_.Get("/healthz", HealthHandlers::HealthCheck);
    router_.Get("/readyz", HealthHandlers::ReadyCheck);
    router_.Get("/metrics", HealthHandlers::Metrics);
    
    // Bucket 操作
    router_.Put("/buckets/{bucket}", [this](HttpRequest& req) {
        return BucketHandlers::CreateBucket(req, *meta_store_);
    });
    router_.Get("/buckets/{bucket}", [this](HttpRequest& req) {
        return BucketHandlers::GetBucket(req, *meta_store_);
    });
    router_.Delete("/buckets/{bucket}", [this](HttpRequest& req) {
        return BucketHandlers::DeleteBucket(req, *meta_store_);
    });
    router_.Get("/buckets", [this](HttpRequest& req) {
        return BucketHandlers::ListBuckets(req, *meta_store_);
    });
    
    // Object 操作
    router_.Put("/buckets/{bucket}/objects/{object}", [this](HttpRequest& req) {
        return ObjectHandlers::PutObject(req, *meta_store_, *data_store_, gc_.get());
    });
    router_.Get("/buckets/{bucket}/objects/{object}", [this](HttpRequest& req) {
        return ObjectHandlers::GetObject(req, *meta_store_, *data_store_);
    });
    router_.Head("/buckets/{bucket}/objects/{object}", [this](HttpRequest& req) {
        return ObjectHandlers::HeadObject(req, *meta_store_);
    });
    router_.Delete("/buckets/{bucket}/objects/{object}", [this](HttpRequest& req) {
        return ObjectHandlers::DeleteObject(req, *meta_store_, *data_store_, gc_.get());
    });
    router_.Get("/buckets/{bucket}/objects", [this](HttpRequest& req) {
        return ObjectHandlers::ListObjects(req, *meta_store_);
    });
    
    // Multipart 操作
    router_.Post("/buckets/{bucket}/multipart/{object}", [this](HttpRequest& req) {
        return MultipartHandlers::InitUpload(req, *meta_store_);
    });
    router_.Put("/buckets/{bucket}/multipart/{object}/{upload_id}/parts/{part_number}",
        [this](HttpRequest& req) {
            return MultipartHandlers::UploadPart(req, *meta_store_, *data_store_, gc_.get());
        });
    router_.Post("/buckets/{bucket}/multipart/{object}/{upload_id}/complete",
        [this](HttpRequest& req) {
            return MultipartHandlers::CompleteUpload(req, *meta_store_, *data_store_, gc_.get());
        });
    router_.Delete("/buckets/{bucket}/multipart/{object}/{upload_id}",
        [this](HttpRequest& req) {
            return MultipartHandlers::AbortUpload(req, *meta_store_, *data_store_, gc_.get());
        });
    
    // 预签名 URL
    router_.Post("/presign", [this](HttpRequest& req) {
        return PresignHandlers::CreatePresignUrl(req, config_.auth.static_tokens);
    });
    
    // 404 处理
    router_.SetNotFoundHandler([](HttpRequest& req) {
        return HttpResponse::NotFound("Resource not found: " + req.Path());
    });
    
    spdlog::info("Routes configured");
}

void Server::Start() {
    if (running_.exchange(true)) {
        return;  // 已经在运行
    }
    
    spdlog::info("Starting MiniS3 server on {}:{}...", 
                 config_.server.listen_ip, config_.server.listen_port);
    
    // 启动 IO 线程池（每个线程一个 EventLoop）
    int io_thread_count = config_.server.io_threads;
    spdlog::info("Creating {} IO threads...", io_thread_count);
    for (int i = 0; i < io_thread_count; ++i) {
        io_threads_.emplace_back([this, i] {
            spdlog::info("IO thread {} started", i);
            EventLoop loop;
            {
                // 注册到 IO loop 列表，供新连接选择
                std::lock_guard<std::mutex> lock(io_loops_mutex_);
                io_loops_.push_back(&loop);
                ++io_loops_ready_;
            }
            io_loops_cv_.notify_one();

            loop.Loop();

            spdlog::info("IO thread {} stopped", i);
        });
    }

    // 等待 IO 线程全部就绪
    if (io_thread_count > 0) {
        std::unique_lock<std::mutex> lock(io_loops_mutex_);
        io_loops_cv_.wait(lock, [this, io_thread_count] {
            return io_loops_ready_ >= static_cast<size_t>(io_thread_count);
        });
    }
    
    // 启动 GC
    if (gc_) {
        gc_->Start();
    }
    
    // 开始监听
    acceptor_->Listen();
    
    spdlog::info("Server started, listening on {}:{}", 
                 config_.server.listen_ip, config_.server.listen_port);
    
    // 主线程事件循环（阻塞）
    main_loop_->Loop();
}

void Server::Stop() {
    if (!running_.exchange(false)) {
        return;  // 已经停止
    }
    
    spdlog::info("Stopping server...");
    
    // 停止接受新连接
    acceptor_.reset();
    
    // 停止 GC
    if (gc_) {
        gc_->Stop();
    }
    
    // 关闭所有连接
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& conn : connections_) {
            conn->ForceClose();
        }
        connections_.clear();
    }
    
    // 停止 IO 线程
    for (auto* loop : io_loops_) {
        if (loop) {
            loop->Quit();
        }
    }
    
    for (auto& thread : io_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    io_loops_.clear();
    io_loops_ready_ = 0;
    
    // 停止主循环
    main_loop_->Quit();
    
    spdlog::info("Server stopped");
}

EventLoop* Server::GetNextLoop() {
    if (io_loops_.empty()) {
        return main_loop_.get();
    }
    
    size_t index = next_loop_index_.fetch_add(1) % io_loops_.size();
    return io_loops_[index];
}

void Server::OnNewConnection(int sockfd, const std::string& peer_addr) {
    // 选择一个 IO 线程的 EventLoop 处理该连接
    EventLoop* loop = GetNextLoop();
    
    auto conn = std::make_shared<HttpConnection>(loop, sockfd, peer_addr);
    // 配置连接：超时、请求体大小、socket buffer
    conn->SetIdleTimeout(config_.limits.connection_idle_timeout);
    conn->SetMaxBodyBytes(config_.limits.max_body_bytes);
    conn->SetSocketBufferSizes(config_.server.recv_buffer_size, config_.server.send_buffer_size);
    
    // 注册回调：请求、流式 body、关闭
    conn->SetRequestCallback(
        [this](const std::shared_ptr<HttpConnection>& c, HttpRequest& req) {
            OnRequest(c, req);
        }
    );

    conn->SetBodyDataCallback(
        [this](const std::shared_ptr<HttpConnection>& c, const char* data, size_t len) {
            OnBodyData(c, data, len);
        }
    );
    
    conn->SetCloseCallback(
        [this](const std::shared_ptr<HttpConnection>& c) {
            OnConnectionClose(c);
        }
    );
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.insert(conn);
    }

    // 跟踪连接计数
    Metrics::GetInstance().IncrementActiveConnections();
    
    conn->Start();
    
    spdlog::debug("New connection from {}", peer_addr);
}

void Server::OnConnectionClose(const std::shared_ptr<HttpConnection>& conn) {
    // 从连接集合中移除
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.erase(conn);

    Metrics::GetInstance().DecrementActiveConnections();

    // 清理该连接的流式上传上下文
    {
        std::lock_guard<std::mutex> upload_lock(uploads_mutex_);
        uploads_.erase(conn->Fd());
    }
    
    spdlog::debug("Connection closed: {}", conn->PeerAddr());
}

void Server::OnRequest(const std::shared_ptr<HttpConnection>& conn, HttpRequest& request) {
    auto start_time = std::chrono::steady_clock::now();

    {
        // 如果该连接之前走了流式上传，先提交写入并把 CAS key 注入请求头
        std::unique_ptr<WriteSession> session;
        std::string expected_sha;
        size_t bytes_written = 0;
        bool streaming = false;

        {
            std::lock_guard<std::mutex> upload_lock(uploads_mutex_);
            auto it = uploads_.find(conn->Fd());
            if (it != uploads_.end()) {
                streaming = it->second.streaming;
                session = std::move(it->second.session);
                expected_sha = std::move(it->second.expected_sha256);
                bytes_written = it->second.bytes_written;
                uploads_.erase(it);
            }
        }

        if (streaming && session) {
            auto commit_result = data_store_->CommitWrite(std::move(session), expected_sha);
            if (!commit_result.ok()) {
                HttpResponse resp = HttpResponse::BadRequest(commit_result.status().message(), request.TraceId());
                conn->Send(std::move(resp));
                return;
            }
            request.SetHeader("X-Internal-CAS-Key", commit_result.value());
            request.SetHeader("X-Internal-Size", std::to_string(bytes_written));
        }
    }
    
    // 路由处理
    HttpResponse response = router_.Handle(request);
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Access log + Metrics
    Logging::AccessLog(request, response, duration.count());
    
    // 发送响应（Send 内部处理文件与普通响应）
    conn->Send(std::move(response));
}

void Server::OnBodyData(const std::shared_ptr<HttpConnection>& conn, const char* data, size_t len) {
    auto& request = conn->Request();

    // 非流式请求直接拼接到内存
    if (!ShouldStreamUpload(request)) {
        request.AppendBody(std::string_view(data, len));
        return;
    }

    // 流式上传：按连接 fd 维护写入会话
    std::lock_guard<std::mutex> lock(uploads_mutex_);
    auto& ctx = uploads_[conn->Fd()];
    if (!ctx.streaming) {
        ctx.streaming = true;
        ctx.expected_sha256 = request.GetHeader("X-Content-SHA256");
        auto begin_result = data_store_->BeginWrite();
        if (!begin_result.ok()) {
            conn->Send(HttpResponse::InternalError("Failed to start upload", request.TraceId()));
            conn->ForceClose();
            return;
        }
        ctx.session = std::move(begin_result.value());
    }

    // 写入 chunk 并累计长度
    auto status = ctx.session->Write(data, len);
    if (!status.ok()) {
        conn->Send(HttpResponse::InternalError("Failed to write upload", request.TraceId()));
        conn->ForceClose();
        return;
    }
    ctx.bytes_written += len;
}

bool Server::ShouldStreamUpload(const HttpRequest& request) const {
    // 仅对 PUT object 与 multipart 走流式写入
    if (request.Method() != HttpMethod::PUT) {
        return false;
    }
    const std::string& path = request.Path();
    return path.find("/objects/") != std::string::npos ||
           path.find("/multipart/") != std::string::npos;
}

size_t Server::ActiveConnections() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    return connections_.size();
}

} // namespace minis3
