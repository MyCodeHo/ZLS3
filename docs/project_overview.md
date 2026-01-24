# MiniS3 项目全景说明

## 1. 项目定位与目标
MiniS3 是一个轻量级对象存储服务，提供类似 S3 的核心能力：Bucket 管理、对象上传/下载/删除、分段上传、预签名 URL、健康检查与指标导出。项目目标是在可控复杂度下实现可部署、可测试、可观测的对象存储原型。

## 2. 架构总览
系统由四个核心层组成：
1) 网络与协议层：基于 epoll 的事件循环、非阻塞 I/O、HTTP 解析与连接管理。
2) API 层：路由分发、中间件、各类 Handler 实现业务语义。
3) 服务与存储层：MetaStore 负责元数据持久化（MySQL），DataStore 负责对象数据存储（CAS 布局），GC 负责垃圾清理。
4) 监控与运维层：Prometheus 指标、日志系统、Docker/脚本工具。

请求路径：
客户端 -> HttpConnection -> HttpRouter -> Handler -> MetaStore/DataStore -> HttpResponse。

## 3. 目录结构解读
- cmd/minis3_server：服务入口，读取配置并启动 Server。
- src/net：网络与协议栈实现（epoll、channel、http parser）。
- src/api：路由与业务处理，含 handlers 和 middleware。
- src/db：MySQL 连接池、事务、元数据访问。
- src/storage：CAS 数据存储、GC 逻辑。
- src/util：配置、日志、指标、加密、UUID 等基础工具。
- docs：接口说明、运维、性能说明。
- scripts：自动化测试与基准脚本。
- tests：单测与集成测试。

## 4. 核心数据模型
### 4.1 Bucket
- name, created_at 等字段。
- 通过 MetaStore 管理创建/删除/列表。

### 4.2 Object
- key、size、etag、content_type、sha256_hash、created_at。
- 存储层为 CAS：对象数据写入文件系统，key 通过 SHA256 作为内容地址。
- 元数据中保存 sha256_hash 与 etag。

### 4.3 Multipart
- upload_id 关联 bucket_id 与 key。
- part 记录包括 part_number、etag、sha256_hash、size、created_at。
- 完成合并时按 part 顺序合并 CAS 文件。

## 5. 关键组件详解
### 5.1 EventLoop 与 epoll
- EventLoop 驱动 Channel 处理读写事件。
- Acceptor 监听新连接并分发给 I/O 线程。

### 5.2 HttpConnection
- 负责连接状态机：解析头部、流式读取 body、发送响应或 sendfile。
- 支持最大 body 限制、空闲连接超时。
- 支持 socket 缓冲区配置以提高吞吐。

### 5.3 HttpParser
- 解析请求行、头部和内容长度。
- 支持流式 body 消费，避免大对象一次性读取。

### 5.4 Router 与 Middleware
- HttpRouter 支持路由匹配与参数解析。
- TraceMiddleware 负责 trace id 贯穿。
- AuthMiddleware 基于静态 token 校验。

### 5.5 MetaStore (MySQL)
- 负责 bucket、object、multipart、part 的 CRUD。
- 支持事务化的 ref_count 更新，配合 CAS 释放。

### 5.6 DataStore (CAS)
- 写入：返回 CAS key（SHA256）。
- 读取：支持文件路径定位。
- 合并：multipart 完成时合并多个分片 CAS 文件。

### 5.7 GC
- 定期清理无引用 CAS blob。
- 与 MetaStore ref_count 结合保证一致性。

## 6. 主要 API 概览
- Health/Ready：/healthz, /readyz
- Bucket：/buckets, /buckets/{bucket}
- Object：/buckets/{bucket}/objects/{object}
- Multipart：/buckets/{bucket}/multipart/{object} 以及对应 parts/complete
- Presign：/presign
- Metrics：/metrics

详见接口文档：docs/api.md。

## 7. 配置说明
- configs/server.yaml：默认生产配置。
- configs/server.local.yaml：本地开发配置。
- 核心配置包含 server、storage、mysql、auth、limits、multipart、log、metrics、gc。

## 8. 指标与日志
- Prometheus 指标：请求计数、延迟、吞吐、连接数、存储容量、GC 队列长度等。
- 日志：访问日志与错误日志可分离输出。

## 9. 测试与运行
- 单元测试与集成测试：ctest。
- API 测试：scripts/test_api.sh。
- Multipart 测试：scripts/test_multipart.sh。
- 性能脚本：scripts/bench_upload.sh / bench_download.sh。

## 10. 典型请求流程示例
PUT 对象：
1) 接收请求并解析头部。
2) 流式读取 body，写入 DataStore 临时文件并计算 SHA256。
3) 写入 MetaStore（对象元数据 + ref_count 事务）。
4) 返回 ETag、大小等信息。

GET 对象：
1) MetaStore 获取对象元数据。
2) DataStore 查找 CAS 文件。
3) 通过 sendfile 发送数据，提升吞吐。

## 11. 常见问题与排障
- 启动失败：检查 MySQL 连接、配置路径。
- 404：检查 bucket/object 是否存在。
- 性能异常：查看磁盘 I/O、MySQL 连接池、socket 缓冲区配置。

## 12. 你需要掌握的重点
- Reactor 模型与 epoll 处理流程。
- HTTP 协议解析与流式 body 处理方式。
- CAS 数据布局与 ref_count + GC 的一致性设计。
- MetaStore 与 DataStore 的分工与事务边界。
- API 路由与中间件对请求的影响。

阅读入口建议：
1) src/server/server.cpp
2) src/net/http/http_connection.cpp
3) src/api/handlers/*
4) src/db/meta_store.cpp
5) src/storage/data_store.cpp
6) docs/api.md / docs/ops.md / docs/perf.md
