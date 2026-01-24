# 单机对象存储/文件分发服务设计文档
**固定选型**：C++17/20 + Linux + `epoll` 自写 Reactor + **MySQL(InnoDB)** 元数据 + **REST/HTTP** 接口  

---

## 0. 项目定位（写给简历/面试的“含金量”）
MiniS3-CPP 是一个单机对象存储（Object Storage）服务，支持：
- 大文件 **流式上传/下载**
- **Range** 断点下载
- **Multipart** 分片上传与断点续传
- **秒传/去重（CAS 内容寻址）**
- **幂等**（应对客户端重试）
- **预签名 URL**（临时下载链接）
- 可观测性（Prometheus 指标 + 结构化日志）
- 完整压测与性能报告

核心展示点：
- `epoll` 自写 Reactor、连接状态机、HTTP 解析与回写机制
- sendfile/零拷贝下载、上传流式落盘、文件系统布局设计
- MySQL 元数据建模、事务一致性、并发与 GC 策略
- 工程化：配置、测试、部署、bench 工具与指标

---

## 1. 范围与非目标（明确边界）
### 1.1 MVP 必做（能跑起来、能压测、能写简历）
- REST API：PUT/GET/HEAD/DELETE 对象
- Range 下载
- Multipart：init/uploadPart/complete/abort
- CAS 去重（按 sha256）+ 引用计数
- MySQL 元数据 + 事务
- 基础鉴权（token / api key）
- Prometheus metrics + JSON access log
- Docker（仅用于快速启动 MySQL/服务，不涉及 k8s）

### 1.2 非目标（避免需求膨胀）
- 不做多节点一致性、复制、选主
- 不追求完整 S3 协议兼容（仅做核心能力）
- 不做复杂 Web UI（可选提供 CLI 或简单页面）

---

## 2. 总体架构（模块分层）
### 2.1 分层图（逻辑）
- **Net 层（epoll Reactor）**
  - Acceptor、EventLoop、Connection、Buffer、Timer、Notifier(eventfd)
  - HTTP Parser（请求行/headers/body 流式）
- **HTTP Router/Controller 层**
  - 路由匹配、参数解析、统一错误返回
  - 认证/鉴权中间件、trace_id 注入
- **Service 层（业务编排）**
  - BucketService、ObjectService、MultipartService、AuthService、PresignService
- **Storage 层**
  - DataStore（CAS 文件写入/读取、临时文件、合并分片、GC）
  - MetaStore（MySQL 访问层 + 事务）
- **Background 层**
  - Multipart 过期清理、CAS ref_count=0 的异步删除、临时文件清理
- **Observability**
  - metrics exporter、access/error log

### 2.2 关键原则
- 在线请求路径尽量短：**I/O 线程只做网络 + 轻量解析**，重操作交给 worker。
- 大对象不进内存：上传/下载采用**流式**，避免把 body 全读到 RAM。
- 数据一致性用 MySQL 事务保障；CAS GC 采用后台异步方式避免阻塞请求。

### 2.3 C++17/20 特性应用
- **std::optional**：元数据查询结果返回
- **std::string_view**：HTTP header 解析零拷贝
- **std::filesystem**：CAS 路径管理
- **std::variant**：统一错误处理
- **concepts**（C++20）：模板约束（可选）
- **coroutine**（C++20）：异步IO改造（高级扩展）
- **智能指针**：RAII 资源管理（连接、文件描述符）

---

## 3. 项目结构（仓库目录规范）
> 这是“给 AI 生成可执行项目”的骨架规范：每个目录职责清晰、接口边界可生成。

```
MiniS3-CPP/
  CMakeLists.txt
  README.md
  docs/
    design.md              # 本文档
    api.md                 # REST API 说明（状态码/示例）
    perf.md                # 压测方法/环境/结果
    ops.md                 # 部署与运维（Docker/配置/监控）
  configs/
    server.example.yaml
  cmd/
    minis3_server/         # main()：启动配置、创建服务、启动 eventloops
  src/
    net/
      epoll/
        event_loop.*       # epoll_wait 主循环、事件分发
        acceptor.*         # 监听 socket、accept、fd 设置 nonblock
        channel.*          # fd 与事件回调绑定（可选）
        notifier.*         # eventfd 唤醒机制
        timer_wheel.*      # 连接超时/定时任务（建议）
      http/
        http_parser.*      # 解析 request line/headers，支持 chunked
        http_request.*     # Request 数据结构（method/path/headers）
        http_response.*    # Response（status/headers/body 或 file）
        http_router.*      # 路由表与 handler 注册
        http_connection.*  # connection 状态机（READ_HDR/READ_BODY/WRITE）
        mime.*             # content-type 推断（可选）
      buffer/
        byte_buffer.*      # ring buffer 或线性 buffer
    util/
      config.*             # YAML/JSON 解析
      logging.*            # spdlog/fmt 封装，JSON access log
      metrics.*            # prometheus exporter
      crypto.*             # sha256/hmac（可用 openssl）
      uuid.*               # upload_id 生成
      fs.*                 # 目录创建、rename 原子操作等
      status.*             # 统一错误码
    db/
      mysql_pool.*         # MySQL 连接池
      mysql_tx.*           # RAII 事务封装
      meta_store.*         # buckets/objects/cas/multipart/idempotency CRUD
    storage/
      data_store.*         # CAS 写入/读取、temp、multipart 合并、delete
      cas_layout.*         # cas 路径规则
      gc.*                 # ref_count=0 的删除队列
    service/
      auth_service.*
      bucket_service.*
      object_service.*
      multipart_service.*
      presign_service.*
    api/
      handlers/
        bucket_handlers.*
        object_handlers.*
        multipart_handlers.*
        presign_handlers.*
      middleware/
        auth_middleware.*
        trace_middleware.*
        rate_limit_middleware.*   # 可选
  scripts/
    gen_test_file.sh
    bench_upload.sh
    bench_download.sh
    mysql_init.sql
  docker/
    Dockerfile
    docker-compose.yaml
  tests/
    unit/
    integration/
```

**生成约束（给 AI 的明确要求）**
- Net 层不得直接访问 MySQL；只能通过 Service → MetaStore
- DataStore 不知道 HTTP；只暴露 read/write/merge/delete 接口
- 所有 handler 必须返回统一 JSON 错误结构（除下载二进制流）
- 所有请求必须打 access log，并带 trace_id

---

## 4. 线程模型与职责（epoll Reactor 方案）
### 4.1 线程模型（推荐固定为此）
- **1 个 Acceptor 线程**（或主线程）：只负责 accept 新连接并分发给 I/O EventLoop（round-robin）。
- **N 个 I/O 线程**：每个线程拥有一个 `EventLoop(epoll_fd)`，负责：
  - 读 socket、解析 HTTP 头
  - 管理 Connection 状态机
  - 对大 body 做“流式搬运”（从 socket → 临时文件 / 或投入 worker）
  - 写回响应（小响应 writev；大文件用 sendfile）
- **M 个 Worker 线程池**：
  - MySQL 操作（MetaStore CRUD/事务）
  - hash 计算（可与上传边读边算结合）
  - multipart complete 合并（大量磁盘 IO 可能需要单独队列）
- **后台 GC 线程**：
  - 清理 multipart 过期
  - 清理临时文件
  - ref_count=0 的 CAS 删除（异步）

### 4.2 连接状态机（Connection）
每条连接必须有明确状态，AI 生成代码时以此为准：
- `READING_HEADERS`
- `READING_BODY_STREAM`（大上传）
- `PROCESSING`（任务已投递 worker，I/O 等待结果）
- `WRITING_RESPONSE`
- `SENDING_FILE`（sendfile）
- `CLOSED`

### 4.3 关键机制
- **eventfd notifier**：worker 完成任务 → 将响应投递到对应 I/O loop 的队列 → 写 eventfd 唤醒 epoll，避免跨线程直接操作 epoll。
- **定时器**：连接 idle timeout、上传超时、multipart 过期清理。

### 4.4 性能优化策略
- **零拷贝**：sendfile() 下载、splice() 可选
- **内存池**：Buffer 复用减少内存分配
- **对象池**：Connection/Request/Response 对象复用
- **批量处理**：MySQL batch insert（multipart parts）
- **预分配**：固定 buffer size 避免动态扩容
- **CPU亲和性**：绑定 IO 线程到特定核心（可选）

---

## 5. REST API 设计（“最合适”的接口选择已固定：HTTP/1.1 REST）
> 对象存储的最合适接口就是 REST：天然匹配浏览器/curl、文件直传、Range、CDN 前源站场景。  
> 本节给出“必须实现”的接口与行为约束（AI 可据此实现）。

### 5.1 通用约定
- 编码：UTF-8
- 错误响应：除下载外统一 JSON
  - `{"error": {"code": "InvalidArgument", "message": "...", "trace_id": "..."}}`
- Header：
  - `X-Request-Id`（若客户端不传，服务端生成）
  - `Authorization: Bearer <token>` 或 `X-API-Key: <key>`
- 对象名（object_key）只作为逻辑键，不允许当文件路径使用（防穿越）

### 5.2 对象（单文件）
- `PUT /buckets/{bucket}/objects/{object}`
  - Body：二进制流
  - 可选 Header：
    - `Content-Type`
    - `X-Content-SHA256`（可选，提供则校验）
    - `Idempotency-Key`（强烈建议支持）
- `GET /buckets/{bucket}/objects/{object}`
  - 支持 `Range: bytes=start-end`
  - 返回头：
    - `ETag`
    - `Content-Length`
    - `Accept-Ranges: bytes`
- `HEAD /buckets/{bucket}/objects/{object}`
  - 返回对象元信息（size/etag/content-type/mtime）
- `DELETE /buckets/{bucket}/objects/{object}`

### 5.3 Multipart
- `POST /buckets/{bucket}/multipart/{object}` → `{ "upload_id": "uuid", "expires_at": ... }`
- `PUT /buckets/{bucket}/multipart/{object}/{upload_id}/parts/{part_number}`
  - part_number 从 1..10000
  - Body：分片数据流
  - 返回 `{ "etag": "...", "size": ... }`
- `POST /buckets/{bucket}/multipart/{object}/{upload_id}/complete`
  - Body：`{ "parts": [{"part_number":1,"etag":"..."}, ...] }`
  - 返回对象最终 etag/size
- `DELETE /buckets/{bucket}/multipart/{object}/{upload_id}`（abort）

### 5.4 预签名 URL
- `POST /presign`
  - Body：`{ "bucket":"b", "object":"o", "method":"GET", "ttl_seconds": 600 }`
  - 返回：`{ "url": "...", "expires_at": ... }`
- 使用：`GET /buckets/{b}/objects/{o}?expires=...&sig=...&method=GET`

### 5.6 错误处理规范
- **网络层**：返回错误码（EAGAIN/EINTR 重试，其他断开连接）
- **业务层**：使用 Status/Result<T> 模式，禁止异常（或仅RAII）
- **资源清理**：所有资源使用 RAII（文件、连接、锁）
- **事务回滚**：MySQL 异常自动回滚，记录详细日志

---

## 6. 数据落盘（CAS 内容寻址）与文件系统布局
### 6.1 CAS 规则（固定）
- hash：SHA-256（十六进制 64 字符）
- 路径：`{data_dir}/cas/aa/bb/<sha256>.blob`（按前 4 位���层）
- 临时上传文件：`{tmp_dir}/upload/<uuid>.part`
- multipart 临时目录：`{tmp_dir}/multipart/<upload_id>/part_<n>.part`

### 6.2 写入流程（单文件 PUT）
1. I/O 线程接收请求头，创建临时文件（uuid）
2. 进入 `READING_BODY_STREAM`：
   - 循环读 socket → 写临时文件（分块）
   - 同步更新 sha256 ctx（边读边算）
3. body 完成：
   - 得到 sha256（cas_key）与 size
   - 若客户端提供 X-Content-SHA256，校验不一致则删除临时文件并返回 400
4. 将临时文件 **原子 rename** 到 CAS 最终路径：
   - 如果 CAS 已存在同 hash：删除临时文件（实现去重）
   - 否则 rename
5. 调用 MetaStore 事���：更新 cas_blobs/ref_count + upsert objects
6. 返回 200/201 + ETag

### 6.3 读取流程（GET 下载）
1. MetaStore 查询 objects，拿到 cas_key、size、content_type、etag
2. 打开 CAS 文件 fd
3. 若 Range：
   - 解析合法范围，返回 206，设置 Content-Range
4. 使用 `sendfile()` 回写（状态 `SENDING_FILE`）
5. 记录 bytes_out、latency

---

## 7. MySQL 元数据设计（表、索引、事务）
> MySQL 是“事实标准”的后端元数据存储，面试认可度高。此处定义“必须字段与事务逻辑”。

### 7.1 必要表（最小集合）
- `buckets`
- `objects`（bucket + object_key → cas_key）
- `cas_blobs`（cas_key → size + ref_count）
- `multipart_uploads`
- `multipart_parts`
- `idempotency_records`

### 7.2 索引要求（性能关键）
- `objects`：`UNIQUE(bucket_id, object_key)`；`INDEX(cas_key)`
- `multipart_parts`：`PRIMARY(upload_id, part_number)`
- `cas_blobs`：`PRIMARY(cas_key)`
- `idempotency_records`：`PRIMARY(idempotency_key)` + `expires_at` 索引（便于��理）

### 7.2.1 索引优化建议
- `objects`：`INDEX(cas_key)` 用于 ref_count 统计
  - `INDEX(created_at)` 可选，用于按时间查询
- `multipart_uploads`：`INDEX(expires_at)` 用于 GC 扫描
  - `INDEX(bucket_id, object_key)` 避免重复创建
- `idempotency_records`：`INDEX(expires_at)` 用于定期清理
- `cas_blobs`：考虑添加 `updated_at` 字段用于监控

### 7.2.2 分区建议（扩展）
- `idempotency_records` 按 `expires_at` 范围分区
- `multipart_uploads` 按月份分区（历史归档）

### 7.3 PUT（单文件）事务规范（必须按此保证一致）
事务内必须做到：
- cas_blobs ref_count 增加（若新 CAS 则插入）
- objects upsert 映射到 cas_key
- 若对象覆盖旧值：需要对旧 cas_key ref_count -1（并可能触发 GC）

**对象覆盖处理规范（重点）**
- 在事务中先 `SELECT cas_key FROM objects WHERE bucket_id=? AND object_key=? FOR UPDATE`
- 若存在 old_cas_key 且 old_cas_key != new_cas_key：
  - old ref_count -1
  - new ref_count +1
- 最终 upsert objects 指向 new_cas_key
- 事务提交后，若 old ref_count 变 0，把 old_cas_key 放入 GC 队列（异步删文件）

### 7.4 DELETE 事务规范
- 锁定 objects 行 → 获取 cas_key
- 删除 objects
- cas_blobs ref_count -1
- 若 ref_count=0：加入 GC 队列（异步删除 CAS 文件与行）

---

## 8. Multipart 合并策略（固定为“合并生成最终 blob”，便于可执行）
为确保项目易实现且行为清晰，**complete 时将各 part 串接生成一个最终 CAS blob**（而不是维护分段索引）。

### 8.1 complete 处理步骤
1. 校验 upload_id、bucket、object 匹配
2. 按 part_number 顺序读取 multipart_parts 的 cas_key/size，校验与客户端提交的 etag 列表一致
3. 在 tmp 目录创建 `tmp/merge/<uuid>.part`
4. 依次读每个 part 的 CAS 文件，写入 merge 文件，并边写边计算最终 sha256（作为最终 cas_key）
5. 原子 rename 到最终 CAS 路径（去重逻辑同单文件）
6. MetaStore 事务：
   - 将最终对象写入 objects（覆盖策略同 PUT）
   - parts 所引用 CAS 的 ref_count 应在 uploadPart 时已 +1，或在 complete 时统一 +1（二选一，见 8.2）

### 8.2 ref_count 策略（固定，避免泄露）
建议固定为：
- **uploadPart 完成后：对 part 的 cas_key 做 ref_count +1，并记录 multipart_parts**
- complete 成功后：
  - 将 upload 记录删除（ON DELETE CASCADE 删除 parts 记录）
  - 对 parts 的 cas_key 做 ref_count -1（因为它们仅是临时组成件，不再需要；最终对象使用新 cas_key）
  - 最终对象 cas_key ref_count +1（由对象 upsert 逻辑完成）

这样 multipart 不会在磁盘留下大量“永远 ref_count>0 的 part blobs”。

---

## 9. 幂等（Idempotency-Key）规范
### 9.1 适用接口
- 单文件 PUT：强烈建议支持
- uploadPart：可选支持（更复杂，但能加分）

### 9.2 规则
- Idempotency-Key 作为主键存储在 `idempotency_records`
- 服务端对请求做 request_hash（method+path+content-length+optional content-sha）
- 若同 key 已存在：
  - request_hash 一致 → 返回相同响应（含 etag）
  - 不一致 → 409 Conflict
- 记录设置 expires_at（例如 24h），后台定期清理

---

## 10. 预签名 URL（Presign）规范
- `sig = HMAC_SHA256(secret, method+"\n"+path+"\n"+expires)`
- 请求带 `expires` 与 `sig`，校验未过期、签名一致即可绕过 Authorization
- presign 只开放 GET（MVP），后续可扩展 PUT

## 10.5 安全加固
- **路径遍历防护**：严格校验 bucket/object_key 格式
- **请求大小限制**：防止内存耗尽攻击
- **频率限制**：rate_limit_middleware 限流
- **SQL注入防护**：prepared statement
- **临时文件清理**：异常中断后的孤儿文件清理
- **签名时间窗口**：presign URL 最长 7 天
---

## 11. 可观测性（必须做成“可对外证明”的成果）
### 11.1 Prometheus 指标（最少集合）
- `minis3_http_requests_total{method,code}`
- `minis3_http_request_duration_seconds_bucket{method,route}`
- `minis3_active_connections`
- `minis3_upload_bytes_total`
- `minis3_download_bytes_total`
- `minis3_storage_used_bytes`
- `minis3_objects_count`
- `minis3_cas_blobs_count`
- `minis3_gc_queue_length`

### 11.2 日志（访问日志必须结构化）
access log 字段建议固定：
- ts, trace_id, client_ip, method, path, route, status
- latency_ms, bytes_in, bytes_out
- bucket, object（如能提取）
- user/ak（脱敏）

### 11.3 告警规则建议
- **错误率**：5xx 错误率超过 1% 持续 5 分钟
- **延迟**：P99 延迟超过 5s 持续 10 分钟
- **存储**：磁盘使用率超过 85%
- **连接数**：活跃连接数超过配置上限的 90%
- **GC 队列**：gc_queue_length 超过 10000（可能删除缓慢）
- **MySQL**：连接池耗尽、慢查询超过 1s

---

## 12. 配置与运维（可运行的最低要求）
### 12.1 配置文件（server.yaml）必须支持
- listen.ip, listen.port
- io_threads, worker_threads
- data_dir, tmp_dir
- mysql: host/port/user/password/db/pool_size
- auth: static_token / api_keys
- limits:
  - max_body_bytes
  - max_concurrent_uploads
  - max_concurrent_downloads
  - connection_idle_timeout_seconds
- log: level, access_log_path, error_log_path
- metrics: enable, listen_addr

### 12.2 Docker 目标
- docker-compose 启动 MySQL + minis3 服务
- volume 挂载 data_dir、tmp_dir、mysql 数据目录
- `scripts/mysql_init.sql` 初始化 schema

### 12.3 生产部署建议（扩展）
- **文件系统**：XFS 更适合大文件（推荐），ext4 也可
- **磁盘**：SSD 用于 MySQL，HDD/SSD 均可用于 CAS 存储
- **目录挂载**：data_dir 和 tmp_dir 可分别挂载不同磁盘
- **备份策略**：MySQL 定期全量 + binlog，CAS 文件可用 rsync
- **负载均衡**：nginx/haproxy 前置（多实例时）
- **日志轮转**：logrotate 配置避免磁盘占满

### 12.5 可测试性设计
- **依赖注入**：Service 层接口化，方便 mock MetaStore/DataStore
- **时钟抽象**：时间相关逻辑使用 Clock 接口（方便时间控制）
- **配置覆盖**：测试环境使用内存 SQLite 替代 MySQL（可选）
- **日志级别**：测试时可调整为 DEBUG 查看详细流程

---

## 13. 测试计划（让项目“可信、可交付”）
### 13.1 单元测试（建议）
- HTTP Range 解析、签名校验、路径合法性校验
- CAS 路径映射函数（sha256->path）
- MetaStore 事务逻辑（可用测试库）

### 13.2 集成测试（必须有）
- PUT → HEAD → GET 校验内容一致
- Range GET 校验分段一致
- Multipart：init → uploadPart(并发) → complete → GET 校验 hash
- 幂等：同 Idempotency-Key 重试返回一致结果
- 删除：DELETE 后 GET 返回 404，CAS 引用计数正确（通过 metrics 或 DB 查询）

---

## 14. 压测与性能报告计划（必须输出 docs/perf.md）
### 14.1 压测场景（固定 3 个）
1) **小文件上传**（64KB）  
- 并发：100/500/1000  
- 指标：QPS、P95/P99、CPU、磁盘写
2) **大文件下载**（1GB）  
- 并发：4/8/16  
- 指标：带宽 MB/s、CPU（展示 sendfile 优势）
3) **混合读写**（70% GET / 30% PUT）  
- 指标：错误率、尾延迟、稳定性

### 14.2 perf.md 必须包含
- 硬件/OS/文件系统/MySQL 配置
- 服务配置（io_threads/worker_threads）
- 压测工具���命令
- 数据与图表（可用表格）
- 分析结论：瓶颈与优化点（例如：磁盘、MySQL、网络）

---

## 15. 开发计划（按周里程碑，确保“可执行”）
> 目标：8 周做出一个能写简历、能演示、能压测的完整项目。

### Week 0：准备（1–2 天）
- 确认依赖：openssl、yaml-cpp、mysqlclient（或 mysql-connector-c）、spdlog/fmt、picohttpparser（可选）
- MySQL schema 落地 + docker-compose 起 MySQL
- 仓库骨架与 CI（build + lint 可选）

### Week 1：Reactor + HTTP 最小闭环
- epoll event loop、accept、non-blocking、连接管理
- HTTP request line + headers 解析
- 实现健康检查：GET /healthz
- access log + trace_id

交付：服务可启动、可接受请求、可打印日志

### Week 2：对象 PUT/GET（不含 MySQL）
- DataStore：临时文件写入、atomic rename、CAS 路径
- GET：从 CAS 文件 sendfile 输出
- HEAD：返回基本 header（size/etag）

交付：单机文件可上传下载（先不做元数据）

### Week 3：接入 MySQL 元数据（形成可用产品）
- MySQL 连接池 + MetaStore CRUD
- PUT 完成后写 objects/cas_blobs（事务）
- GET/HEAD 通过 DB 找 cas_key
- DELETE 完整事务 + GC 队列（可先只 DB，文件删除可延后）

交付：对象存储“可用”版本（可演示）

### Week 4：Range + Metrics
- Range 解析 + 206 Partial Content
- Prometheus exporter：核心指标上线
- 修正连接超时、错误码统一化

交付：断点下载 + 可观测性

### Week 5：Multipart（init/uploadPart）
- init 创建 upload_id
- uploadPart：流式落盘、计算 part etag、写 multipart_parts
- 并发上传 part 的正确性与限流（按 upload_id 或全局）

交付：分片上传可用

### Week 6：Multipart complete + 去重（CAS）完善
- complete：按顺序合并生成最终 blob（流式）
- 覆盖对象的 old_cas_key ref_count 处理
- 完整 GC：ref_count=0 异步删除 cas 文件与 DB 行（安全重试）

交付：大文件断点续传链路完整

### Week 7：幂等 + Presign
- Idempotency-Key（至少对 PUT）
- Presign GET
- 安全与鲁棒性：路径校验、请求大小限制、并发限制

交付：可靠性与可运维增强

### Week 8：压测、perf.md、文档完善
- scripts/bench + 固化测试数据
- docs/perf.md 输出结果与分析
- README 完整（快速开始、接口示例、架构图）

交付：可写简历的最终版本

## 15.5 故障处理与恢复
### 上传中断
- **临时文件**：启动时扫描并清理超过24h的 tmp 文件
- **Multipart 孤儿**：定期扫描 expires_at 过期的 upload_id

### 数据一致性
- **部分写入**：rename 前校验文件大小与 Content-Length 一致
- **重复上传**：CAS 文件已存在时，校验文件大小是否匹配
- **并发覆盖**：objects 表使用 `SELECT FOR UPDATE` 行锁

### 服务重启
- **连接优雅关闭**：SIGTERM 信号处理，等待当前请求完成
- **进行中的上传**：客户端需重试（返回 503）

---

## 16. 交付验收标准（Definition of Done）
以下条件全部满足，才算项目“可用可执行”：
1. docker-compose 一键启动 MySQL + 服务
2. PUT/GET/HEAD/DELETE 全通，且 GET 支持 Range
3. Multipart 全流程可跑通（并发上传 parts）
4. CAS 去重可验证（相同文件重复上传不重复占用磁盘）
5. MySQL 事务保证 ref_count 正确（覆盖与删除无泄漏）
6. metrics 可抓取，access log 结构化可检索
7. docs/api.md + docs/perf.md + docs/ops.md 完整
8. 至少 1 组集成测试可自动运行

### 16.5 README.md 必需章节
1. 项目简介（一句话 + 架构图）
2. 核心特性（带 ✓ 的功能列表）
3. 快速开始（docker-compose up）
4. API 示例（curl 命令）
5. 性能数据（简化版 perf.md）
6. 技术栈（图标展示）
7. 架构设计（链接到 docs/design.md）
8. 开发指南（构建、测试、贡献）

---

## 17. 技术决策（已确定）

### 17.1 第三方库选择
| 功能 | 库 | 版本要求 | 说明 |
|------|-----|---------|------|
| HTTP解析 | 自实现 | - | 展示底层能力，使用 `std::string_view` 零拷贝 |
| 加密 | OpenSSL | >= 1.1.1 | sha256/hmac，工业标准 |
| 配置 | yaml-cpp | >= 0.6 | YAML 配置解析 |
| 日志 | spdlog | >= 1.9 | 高性能结构化日志 |
| JSON | nlohmann/json | >= 3.10 | 响应序列化 |
| MySQL | libmysqlclient | >= 8.0 | 官方 C API |
| UUID | libuuid | - | upload_id 生成 |

### 17.2 编译与构建
- **编译器**：GCC >= 10 或 Clang >= 12（支持 C++20）
- **构建系统**：CMake >= 3.16
- **文件系统**：开发 ext4，生产可选 XFS

### 17.3 关键常量定义
```cpp
constexpr size_t kMaxHeaderSize = 8 * 1024;          // 8KB 请求头上限
constexpr size_t kMaxBodySize = 5ULL * 1024 * 1024 * 1024;  // 5GB body 上限
constexpr size_t kBufferSize = 64 * 1024;            // 64KB IO buffer
constexpr size_t kMaxPartNumber = 10000;             // 最大分片数
constexpr size_t kMinPartSize = 5 * 1024 * 1024;     // 5MB 最小分片
constexpr int kDefaultIdleTimeout = 60;              // 60s 连接空闲超时
constexpr int kMultipartExpireHours = 24;            // 24h multipart 过期
```

---

## 附录 A：错误码定义

| HTTP Status | Error Code | 说明 |
|-------------|------------|------|
| 400 | InvalidArgument | 请求参数错误 |
| 400 | InvalidRange | Range 格式或范围无效 |
| 400 | InvalidPartNumber | 分片号超出范围 |
| 400 | InvalidPartOrder | complete 时分片顺序错误 |
| 400 | EntityTooLarge | body 超过大小限制 |
| 400 | ChecksumMismatch | SHA256 校验失败 |
| 401 | Unauthorized | 缺少或无效的认证 |
| 403 | AccessDenied | 无权访问 |
| 403 | SignatureExpired | 预签名 URL 已过期 |
| 404 | NoSuchBucket | bucket 不存在 |
| 404 | NoSuchKey | object 不存在 |
| 404 | NoSuchUpload | upload_id 不存在 |
| 409 | BucketAlreadyExists | bucket 已存在 |
| 409 | IdempotencyConflict | 幂等键冲突 |
| 500 | InternalError | 服务器内部错误 |
| 503 | ServiceUnavailable | 服务暂时不可用 |

---

## 附录 B：MySQL Schema

```sql
-- 数据库初始化
CREATE DATABASE IF NOT EXISTS minis3 DEFAULT CHARSET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE minis3;

-- buckets 表
CREATE TABLE buckets (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(63) NOT NULL UNIQUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_name (name)
) ENGINE=InnoDB;

-- CAS blobs 表（内容寻址存储）
CREATE TABLE cas_blobs (
    cas_key CHAR(64) PRIMARY KEY,  -- SHA256 hex
    size BIGINT UNSIGNED NOT NULL,
    ref_count INT UNSIGNED NOT NULL DEFAULT 1,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_ref_count (ref_count),
    INDEX idx_updated_at (updated_at)
) ENGINE=InnoDB;

-- objects 表
CREATE TABLE objects (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    bucket_id BIGINT UNSIGNED NOT NULL,
    object_key VARCHAR(1024) NOT NULL,
    cas_key CHAR(64) NOT NULL,
    size BIGINT UNSIGNED NOT NULL,
    content_type VARCHAR(255) DEFAULT 'application/octet-stream',
    etag VARCHAR(64) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_bucket_key (bucket_id, object_key(255)),
    INDEX idx_cas_key (cas_key),
    INDEX idx_created_at (created_at),
    FOREIGN KEY (bucket_id) REFERENCES buckets(id) ON DELETE CASCADE,
    FOREIGN KEY (cas_key) REFERENCES cas_blobs(cas_key)
) ENGINE=InnoDB;

-- multipart_uploads 表
CREATE TABLE multipart_uploads (
    upload_id CHAR(36) PRIMARY KEY,  -- UUID
    bucket_id BIGINT UNSIGNED NOT NULL,
    object_key VARCHAR(1024) NOT NULL,
    content_type VARCHAR(255) DEFAULT 'application/octet-stream',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    INDEX idx_bucket_key (bucket_id, object_key(255)),
    INDEX idx_expires_at (expires_at),
    FOREIGN KEY (bucket_id) REFERENCES buckets(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- multipart_parts 表
CREATE TABLE multipart_parts (
    upload_id CHAR(36) NOT NULL,
    part_number INT UNSIGNED NOT NULL,
    cas_key CHAR(64) NOT NULL,
    size BIGINT UNSIGNED NOT NULL,
    etag VARCHAR(64) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (upload_id, part_number),
    FOREIGN KEY (upload_id) REFERENCES multipart_uploads(upload_id) ON DELETE CASCADE,
    FOREIGN KEY (cas_key) REFERENCES cas_blobs(cas_key)
) ENGINE=InnoDB;

-- idempotency_records 表
CREATE TABLE idempotency_records (
    idempotency_key VARCHAR(64) PRIMARY KEY,
    request_hash CHAR(64) NOT NULL,
    response_status INT NOT NULL,
    response_body TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    INDEX idx_expires_at (expires_at)
) ENGINE=InnoDB;

-- api_keys 表（简单鉴权）
CREATE TABLE api_keys (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    key_id VARCHAR(32) NOT NULL UNIQUE,
    key_secret VARCHAR(64) NOT NULL,
    name VARCHAR(255),
    enabled BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_key_id (key_id)
) ENGINE=InnoDB;

-- 插入默认测试 API Key
INSERT INTO api_keys (key_id, key_secret, name) VALUES 
('test-key-id', 'test-key-secret', 'Test API Key');

-- 插入默认 bucket
INSERT INTO buckets (name) VALUES ('default');
```

---

## 附录 C：配置文件示例

```yaml
# server.yaml
server:
  listen_ip: "0.0.0.0"
  listen_port: 8080
  io_threads: 4
  worker_threads: 8

storage:
  data_dir: "/var/lib/minis3/data"
  tmp_dir: "/var/lib/minis3/tmp"

mysql:
  host: "127.0.0.1"
  port: 3306
  user: "minis3"
  password: "minis3_password"
  database: "minis3"
  pool_size: 16
  connect_timeout: 5
  read_timeout: 30

auth:
  enabled: true
  static_tokens:
    - "dev-token-12345"

limits:
  max_body_bytes: 5368709120  # 5GB
  max_header_bytes: 8192
  max_concurrent_uploads: 100
  max_concurrent_downloads: 500
  connection_idle_timeout: 60
  request_timeout: 3600  # 1h for large uploads

multipart:
  min_part_size: 5242880  # 5MB
  max_part_number: 10000
  expire_hours: 24

log:
  level: "info"  # debug, info, warn, error
  access_log_path: "/var/log/minis3/access.log"
  error_log_path: "/var/log/minis3/error.log"
  json_format: true

metrics:
  enabled: true
  listen_port: 9090
  path: "/metrics"

gc:
  enabled: true
  interval_seconds: 300
  batch_size: 100
```