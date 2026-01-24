# MiniS3 理论知识与文件依赖说明

> 目的：汇总项目使用的核心理论知识，并逐文件说明“提供的方法/职责、依赖关系、被谁使用、与其他文件如何协作”。

---

## 一、理论知识总览

### 1. Reactor 模型与 epoll
- 使用单线程事件循环管理 I/O，多连接复用。
- epoll 监听可读/可写事件，回调驱动处理。
- 通过 Acceptor 接收新连接，分发到 I/O 线程上的 EventLoop。

### 2. 非阻塞 I/O 与连接状态机
- socket 设为非阻塞。
- 连接状态机区分“读头、读体、写响应、发送文件”等阶段。
- 空闲连接超时自动回收。

### 3. HTTP/1.1 解析
- 按“请求行 -> 头部 -> body”解析。
- 通过 Content-Length 判断 body 长度。
- 解析器支持流式消费 body，避免大请求占用内存。

### 4. 流式上传（Streaming Upload）
- 读取 body 的同时写入临时文件并计算 SHA256。
- 上传完成后将临时文件落盘到 CAS，写入元数据。
- 可以降低内存峰值，提高大文件吞吐。

### 5. sendfile 与零拷贝
- GET 请求通过 sendfile 将文件从磁盘直接发送到 socket。
- 减少用户态拷贝，提升吞吐。

### 6. CAS（内容寻址存储）
- 以 SHA256 作为内容地址 key。
- 数据落盘路径由哈希分层目录组织（两级目录）。
- 支持天然去重。

### 7. 引用计数与 GC
- 元数据中维护 CAS blob 的 ref_count。
- 删除对象时减少引用计数；为 0 时加入 GC 队列。
- GC 周期性删除无引用数据并同步元数据。

### 8. MySQL 事务与连接池
- 元数据 Create / Read / Update / Delete 以事务保证一致性。
- 使用连接池降低频繁建连开销。

### 9. 认证与中间件
- Bearer Token 静态认证。
- Trace 中间件为请求生成 trace id。

### 10. 监控与日志
- Prometheus 指标导出 QPS、延迟、吞吐等。
- 访问日志与错误日志分离，支持 JSON 格式。

---

## 二、模块协作总览

数据路径（PUT）：
HttpConnection -> HttpParser -> Router -> Handler -> DataStore/MetaStore -> HttpResponse

数据路径（GET）：
HttpConnection -> Router -> Handler -> MetaStore -> DataStore -> sendfile

元数据路径：
Handler/Service -> MetaStore -> MySQLPool/MySQLTx

存储路径：
Handler -> DataStore -> CASLayout + FS

---

## 三、逐文件说明

以下内容按“提供/方法、依赖、被使用、协作关系”描述。

### cmd/minis3_server/main.cpp
- 提供：`main()`、`PrintUsage()`、`PrintVersion()`。
- 依赖：Server、Config、spdlog、signal。
- 被使用：作为服务进程入口。
- 协作：读取配置 -> 初始化 Server -> 启动 EventLoop。

### configs/server.yaml
- 提供：生产默认配置。
- 依赖：无（配置文件）。
- 被使用：Config::LoadFromFile。
- 协作：Server/MetaStore/DataStore/GC/metrics 读取参数。

### configs/server.example.yaml
- 提供：配置模板与注释说明。
- 依赖：无。
- 被使用：文档与参考。
- 协作：与 server.yaml 保持一致结构。

### configs/server.local.yaml
- 提供：本地开发配置（端口/目录/连接池等）。
- 依赖：无。
- 被使用：本地运行与测试脚本。
- 协作：与 server.yaml 结构一致。

### docs/api.md
- 提供：API 接口说明与示例。
- 依赖：无。
- 被使用：开发/测试参考。

### docs/ops.md
- 提供：部署与运维说明（Docker、启动方式等）。
- 依赖：无。
- 被使用：运维与本地调试。

### docs/perf.md
- 提供：性能目标、测试方法、调优建议与实测结果。
- 依赖：无。
- 被使用：性能验证与优化参考。

### docs/project_overview.md
- 提供：项目全景说明。
- 依赖：无。
- 被使用：快速理解项目。

### docs/interview_qa.md
- 提供：面试问答准备材料。
- 依赖：无。
- 被使用：面试准备。

### scripts/bench_download.sh
- 提供：下载性能基准测试。
- 依赖：curl、bc、numfmt。
- 被使用：perf.md 与性能验证。
- 协作：需要对象已存在。

### scripts/bench_upload.sh
- 提供：上传性能基准测试。
- 依赖：curl、bc、numfmt。
- 被使用：perf.md 与性能验证。
- 协作：自动创建 bucket 并并发上传。

### scripts/gen_test_file.sh
- 提供：生成测试文件（随机内容）。
- 依赖：dd、sha256sum。
- 被使用：性能测试与功能测试。

### scripts/install_deps.sh
- 提供：依赖安装脚本（系统库/构建工具）。
- 依赖：系统包管理器。
- 被使用：初始化开发环境。

### scripts/mysql_init.sql
- 提供：MySQL 初始化表结构与索引。
- 依赖：MySQL。
- 被使用：数据库初始化。

### scripts/quickstart.sh
- 提供：一键启动流程（Docker/MySQL + 服务）。
- 依赖：docker、curl、cmake。
- 被使用：快速体验。

### scripts/test_api.sh
- 提供：端到端 API 测试。
- 依赖：curl。
- 被使用：功能验证。

### scripts/test_multipart.sh
- 提供：multipart 流程测试。
- 依赖：curl。
- 被使用：分片上传验证。

---

## src/api/handlers

### src/api/handlers/bucket_handlers.h/.cpp
- 提供：`BucketHandlers`，包含 Create/Get/Delete/List 相关处理。
- 依赖：MetaStore、HttpRequest/HttpResponse。
- 被使用：Server::SetupRoutes 注册路由。
- 协作：通过 MetaStore 操作 bucket 元数据。

### src/api/handlers/health_handlers.h/.cpp
- 提供：`HealthHandlers`，包含 healthz/readyz/metrics。
- 依赖：Metrics、HttpResponse。
- 被使用：Server 路由。
- 协作：Metrics 导出；readyz 可扩展为依赖检查。

### src/api/handlers/multipart_handlers.h/.cpp
- 提供：multipart 相关 API（init/upload part/complete/abort/list parts）。
- 依赖：MetaStore、DataStore、GC、HttpRequest/Response。
- 被使用：Server 路由。
- 协作：写入分片、校验 ETag、合并 CAS、更新 ref_count。

### src/api/handlers/object_handlers.h/.cpp
- 提供：对象 PUT/GET/HEAD/DELETE/LIST。
- 依赖：MetaStore、DataStore、GC、HttpConnection（sendfile 相关）、Crypto。
- 被使用：Server 路由。
- 协作：PUT 流式写入、GET sendfile、HEAD 返回元数据。

### src/api/handlers/presign_handlers.h/.cpp
- 提供：预签名 URL 生成。
- 依赖：AuthService、PresignService、HttpRequest/Response。
- 被使用：Server 路由。
- 协作：基于 token 校验与签名参数生成 URL。

## src/api/middleware

### src/api/middleware/auth_middleware.h/.cpp
- 提供：`AuthMiddleware::Handle`。
- 依赖：HttpRequest/Response。
- 被使用：Server::SetupRoutes 中注册。
- 协作：校验 Authorization Bearer token。

### src/api/middleware/trace_middleware.h/.cpp
- 提供：`TraceMiddleware::Handle`。
- 依赖：HttpRequest/Response。
- 被使用：Server::SetupRoutes 中注册。
- 协作：为请求注入 trace id。

---

## src/db

### src/db/mysql_pool.h/.cpp
- 提供：`MySQLPool`（连接池初始化与获取连接）。
- 依赖：MySQL C API、Config。
- 被使用：Server 初始化 MetaStore。
- 协作：MetaStore 与 MySQLTx 使用连接池。

### src/db/mysql_tx.h/.cpp
- 提供：`MySQLTx`（事务封装）。
- 依赖：MySQL 连接。
- 被使用：MetaStore 内部。
- 协作：在 MetaStore 进行多表一致性更新。

### src/db/meta_store.h/.cpp
- 提供：`MetaStore`，负责 bucket/object/multipart/part 的 CRUD 与 ref_count 事务。
- 依赖：MySQLPool/MySQLTx、Status/Result。
- 被使用：Handlers/Services/GC。
- 协作：与 DataStore、GC 保持 CAS 一致性。

---

## src/net/buffer

### src/net/buffer/byte_buffer.h/.cpp
- 提供：`ByteBuffer`（可增长 buffer，支持读写与切片）。
- 依赖：无特殊外部依赖。
- 被使用：HttpConnection/HttpParser。
- 协作：管理 socket 的读写缓冲区。

---

## src/net/epoll

### src/net/epoll/acceptor.h/.cpp
- 提供：`Acceptor`（监听与接收新连接）。
- 依赖：EventLoop、Channel、socket API。
- 被使用：Server。
- 协作：新连接分发到 HttpConnection。

### src/net/epoll/channel.h/.cpp
- 提供：`Channel`（fd 与事件/回调绑定）。
- 依赖：EventLoop。
- 被使用：Acceptor、HttpConnection、Notifier。
- 协作：统一事件回调入口。

### src/net/epoll/event_loop.h/.cpp
- 提供：`EventLoop`（epoll 主循环、定时器、任务队列）。
- 依赖：Channel、TimerWheel、Notifier。
- 被使用：Server、HttpConnection、Acceptor。
- 协作：线程内事件驱动核心。

### src/net/epoll/notifier.h/.cpp
- 提供：`Notifier`（跨线程唤醒 EventLoop）。
- 依赖：eventfd。
- 被使用：EventLoop。
- 协作：将任务投递到目标 loop。

### src/net/epoll/timer_wheel.h/.cpp
- 提供：`TimerWheel`（定时器管理）。
- 依赖：chrono。
- 被使用：EventLoop、HttpConnection（空闲超时）。
- 协作：定时任务触发。

---

## src/net/http

### src/net/http/http_connection.h/.cpp
- 提供：`HttpConnection`。
- 主要方法：`Start`、`Send`、`SendFile`、`SetIdleTimeout`、`SetMaxBodyBytes`、`SetSocketBufferSizes`。
- 依赖：EventLoop、Channel、HttpParser、ByteBuffer、HttpRequest/Response。
- 被使用：Server 创建连接。
- 协作：驱动请求解析与响应发送。

### src/net/http/http_parser.h/.cpp
- 提供：`HttpParser`。
- 主要方法：`Parse`、`Reset`、`HasBody`、`ContentLength`、`ConsumeBody`。
- 依赖：HttpRequest、ByteBuffer。
- 被使用：HttpConnection。
- 协作：解析请求并驱动流式读取。

### src/net/http/http_request.h/.cpp
- 提供：`HttpRequest`（URL、Header、Body、Path 参数、Query 参数）。
- 依赖：无。
- 被使用：Parser、Router、Handlers。

### src/net/http/http_response.h/.cpp
- 提供：`HttpResponse`（状态码、Header、Body）。
- 依赖：无。
- 被使用：Handlers、HttpConnection。

### src/net/http/http_router.h/.cpp
- 提供：`HttpRouter`（路由注册、匹配、中间件链）。
- 依赖：HttpRequest/Response。
- 被使用：Server。
- 协作：将请求分发到 Handler。

---

## src/server

### src/server/server.h/.cpp
- 提供：`Server`（Init/Start/Stop、路由注册、连接管理）。
- 依赖：Config、EventLoop、Acceptor、HttpConnection、Handlers、MetaStore、DataStore、GC、Metrics。
- 被使用：main.cpp。
- 协作：整个服务生命周期与核心调度。

---

## src/service

> 说明：service 层为业务封装，目前主流程主要由 handlers 直接调用 MetaStore/DataStore，service 类保留作为可扩展层。

### src/service/auth_service.h/.cpp
- 提供：`AuthService`（token 校验）。
- 依赖：无。
- 被使用：PresignService；当前路由未直接使用。

### src/service/bucket_service.h/.cpp
- 提供：`BucketService`（bucket CRUD）。
- 依赖：MetaStore。
- 被使用：当前主流程未直接使用。

### src/service/object_service.h/.cpp
- 提供：`ObjectService`（对象 CRUD）。
- 依赖：MetaStore、DataStore。
- 被使用：当前主流程未直接使用。

### src/service/multipart_service.h/.cpp
- 提供：`MultipartService`（分片上传流程封装）。
- 依赖：MetaStore、DataStore。
- 被使用：当前主流程未直接使用。

### src/service/presign_service.h/.cpp
- 提供：`PresignService`（预签名 URL 生成）。
- 依赖：AuthService。
- 被使用：PresignHandlers。

---

## src/storage

### src/storage/cas_layout.h/.cpp
- 提供：`CasLayout`（CAS key -> 文件路径映射，目录分层）。
- 依赖：FS 工具。
- 被使用：DataStore。
- 协作：确保目录均衡分布。

### src/storage/data_store.h/.cpp
- 提供：`DataStore`（写入、读取、合并、删除 CAS 数据）。
- 依赖：CasLayout、Crypto、FS、Status。
- 被使用：Handlers、Services、GC。
- 协作：流式上传、sendfile 读取。

### src/storage/gc.h/.cpp
- 提供：`GarbageCollector`（ref_count 为 0 的 CAS 清理）。
- 依赖：MetaStore、DataStore。
- 被使用：Server 初始化，Handlers 在删除时加入待清理队列。
- 协作：周期性清理与元数据回收。

---

## src/util

### src/util/config.h/.cpp
- 提供：`Config` 及各模块配置结构体，`LoadFromFile/LoadFromString`。
- 依赖：yaml-cpp。
- 被使用：main.cpp、Server。

### src/util/crypto.h/.cpp
- 提供：SHA256/HMAC 等加密工具。
- 依赖：OpenSSL。
- 被使用：DataStore、Presign。

### src/util/fs.h/.cpp
- 提供：文件系统辅助（创建目录、路径处理、读写）。
- 依赖：std::filesystem、POSIX API。
- 被使用：DataStore、CasLayout。

### src/util/logging.h/.cpp
- 提供：日志初始化与格式配置。
- 依赖：spdlog。
- 被使用：Server 初始化。

### src/util/metrics.h/.cpp
- 提供：Prometheus 指标注册与更新。
- 依赖：prometheus-cpp。
- 被使用：Server、Handlers、GC。

### src/util/status.h/.cpp
- 提供：`Status` 与 `Result<T>` 结果封装。
- 依赖：无。
- 被使用：几乎所有业务层与存储层。

### src/util/uuid.h/.cpp
- 提供：UUID 生成。
- 依赖：随机数/系统接口。
- 被使用：Multipart upload_id 生成等。

---

## tests

### tests/CMakeLists.txt
- 提供：测试目标配置。
- 被使用：ctest/构建系统。

### tests/unit/test_byte_buffer.cpp
- 覆盖 ByteBuffer 行为。
- 依赖：ByteBuffer。

### tests/unit/test_cas_layout.cpp
- 覆盖 CasLayout 路径映射。
- 依赖：CasLayout。

### tests/unit/test_crypto.cpp
- 覆盖 SHA256/HMAC。
- 依赖：Crypto。

### tests/unit/test_http_parser.cpp
- 覆盖 HTTP 解析器。
- 依赖：HttpParser、ByteBuffer。

### tests/unit/test_status.cpp
- 覆盖 Status/Result。
- 依赖：Status。

### tests/unit/test_uuid.cpp
- 覆盖 UUID 生成。
- 依赖：UUID。

### tests/integration/test_data_store.cpp
- 覆盖 DataStore 读写与合并流程。
- 依赖：DataStore、CasLayout。

### tests/integration/test_meta_store.cpp
- 覆盖 MetaStore 数据库流程。
- 依赖：MetaStore、MySQL。

---

## 四、依赖方向总结

- net/epoll -> net/http：EventLoop 驱动 HttpConnection。
- api/handlers -> db/storage/util：Handler 调用 MetaStore/DataStore/GC/Status。
- server -> api/handlers + net/http + db/storage/util：聚合核心组件。
- service -> db/storage：业务封装（当前未全部接入主流程）。
- tests -> 各模块：验证核心功能。

---

## 五、建议阅读顺序
1) src/server/server.cpp
2) src/net/http/http_connection.cpp
3) src/api/handlers/object_handlers.cpp
4) src/db/meta_store.cpp
5) src/storage/data_store.cpp
6) docs/api.md / docs/ops.md / docs/perf.md
