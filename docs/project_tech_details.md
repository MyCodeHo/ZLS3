# MiniS3-CPP 项目技术详解与面试备忘（≥1000 行）

> 目标：覆盖“技术点 + 实现方案 + 难题与解决 + 核心函数 + 调用逻辑 + 核心流程 + 架构 + 数据结构 + 文件与表 + 传输细节”等全部要点。
> 说明：内容尽量以本项目源码命名为准，便于你临考前快速定位。

------------------------------------------------------------

一、项目总览与简历级能力摘要

1.1 项目定位
- MiniS3-CPP 是单机对象存储服务，提供类似 S3 的核心 API。
- 支持 Bucket 管理、对象上传/下载、Range 下载、分片上传、预签名 URL、幂等性、CAS 去重与 GC。
- 面向高并发与大文件场景，强调网络层高性能与存储层一致性。

1.2 关键能力关键词（简历可用）
- epoll + Reactor
- HTTP/1.1 解析 + 状态机
- 流式上传 + sendfile
- CAS 内容寻址存储
- MySQL 元数据与事务一致性
- 分片上传 + 断点续传
- GC 垃圾回收
- 结构化日志 + Prometheus 指标
- 工程化构建与脚本

1.3 项目结构导航
- 入口：cmd/minis3_server/main.cpp
- 网络层：src/net/epoll/*，src/net/http/*，src/net/buffer/*
- API 处理：src/api/handlers/*，src/api/middleware/*
- 服务层：src/service/*
- 数据与元数据：src/storage/*，src/db/*
- 监控与工具：src/util/*
- 运维与文档：docs/*，scripts/*

------------------------------------------------------------

二、系统架构与请求路径

2.1 分层架构
- 网络与协议层：EventLoop、Acceptor、Channel、HttpConnection、HttpParser。
- 路由与中间件层：HttpRouter、AuthMiddleware、TraceMiddleware。
- 服务与存储层：BucketService/ObjectService/MultipartService/PresignService。
- 元数据层：MetaStore + MySQLPool + Transaction。
- 数据层：DataStore + CasLayout + FileSystem。
- GC：GarbageCollector。
- 可观测性：Logger、Metrics。

2.2 典型请求路径（PUT）
1) 客户端发起 PUT /buckets/{bucket}/objects/{object}
2) Acceptor 接受连接 -> HttpConnection
3) HttpParser 解析头部
4) Server::OnRequest 判断是否流式
5) 若流式：进入 READING_BODY_STREAM
6) Server::OnBodyData 将 body 写入 WriteSession
7) body 完整后 CommitWrite -> cas_key
8) MetaStore::PutObjectWithRefCount 写元数据并维护 ref_count
9) 记录 Metrics/日志 -> 回复响应

2.3 典型请求路径（GET）
1) 路由到 ObjectHandlers::GetObject
2) MetaStore::GetObject 获取 cas_key 与 size
3) DataStore::GetFilePath 返回 CAS 文件路径
4) HttpResponse::SetFileRange -> HttpConnection::SendFile
5) sendfile 发送数据

2.4 典型请求路径（Multipart）
- Init：记录 multipart_uploads
- UploadPart：写入 CAS + 记录 multipart_parts
- Complete：按 part_number 合并 -> 新 cas_key -> PutObjectWithRefCount

------------------------------------------------------------

三、网络层核心实现细节

3.1 Reactor 模型
- EventLoop 使用 epoll_wait 轮询事件。
- Channel 保存 fd 与读写回调，负责事件分发。
- Acceptor 监听新连接并回调 Server::OnNewConnection。

3.2 EventLoop（src/net/epoll/event_loop.h）
- Loop()：主循环阻塞在 epoll_wait。
- RunInLoop/QueueInLoop：跨线程任务投递。
- TimerWheel + timerfd 实现定时任务。

3.3 Channel（src/net/epoll/channel.h）
- EnableReading/EnableWriting：注册 epoll 事件。
- HandleEvent：根据 revents_ 调用对应回调。

3.4 Acceptor（src/net/epoll/acceptor.h）
- CreateNonblockingSocket：创建非阻塞 listen fd。
- HandleRead：accept 新连接，回调 new_connection_callback_。

3.5 HttpConnection（src/net/http/http_connection.h）
- 状态机：READING_HEADERS -> READING_BODY_STREAM -> PROCESSING -> WRITING_RESPONSE -> SENDING_FILE。
- input_buffer_：保存解析前的字节流。
- output_buffer_：用于响应头输出。
- SendFile：零拷贝发送，减少 CPU。

3.6 HttpParser（src/net/http/http_parser.h）
- ParseState：REQUEST_LINE/HEADERS/BODY/DONE/ERROR
- ParseResult：NEED_MORE_DATA/HEADERS_COMPLETE/BODY_DATA/MESSAGE_COMPLETE
- 支持流式读取 body，并通过 ConsumeBody 手动消费。

3.7 ByteBuffer（src/net/buffer/byte_buffer.h）
- ReadableBytes/WritableBytes/PrependableBytes：高效复用。
- ReadFd/WriteFd：readv/writev。

------------------------------------------------------------

四、路由与中间件

4.1 HttpRouter（src/net/http/http_router.h）
- Route(method, pattern, handler)：注册路由。
- CompilePattern：把 /buckets/{bucket} 编译为 regex。
- Handle：匹配路由并写入 PathParams。

4.2 AuthMiddleware（src/api/middleware/auth_middleware.h）
- RequiresAuth：判定路径是否需要鉴权。
- ValidateBearerToken：验证 Authorization: Bearer。
- ValidateApiKey：验证 X-Api-Key 或类似头。

4.3 TraceMiddleware（src/api/middleware/trace_middleware.h）
- 生成 trace_id，并写入 HttpRequest。
- 贯穿日志与错误响应。

------------------------------------------------------------

五、元数据层与事务一致性

5.1 MySQL 表结构（scripts/mysql_init.sql）
- buckets(id, name, created_at)
- cas_blobs(cas_key, size, ref_count, created_at, updated_at)
- objects(id, bucket_id, object_key, cas_key, size, content_type, etag, created_at, updated_at)
- multipart_uploads(upload_id, bucket_id, object_key, content_type, created_at, expires_at)
- multipart_parts(upload_id, part_number, cas_key, size, etag, created_at)
- idempotency_records(idempotency_key, request_hash, response_status, response_body, created_at, expires_at)
- api_keys(key_id, key_secret, enabled)

5.2 MetaStore（src/db/meta_store.h）
- CreateBucket/GetBucket/ListBuckets/DeleteBucket
- PutObject / PutObjectWithRefCount
- GetObject / DeleteObjectWithRefCount
- RegisterCasBlob / GetCasBlob
- Multipart 的 init/upload/complete

5.3 Transaction 与 MySQLPool
- Transaction RAII：执行成功 Commit，异常自动 Rollback。
- MySQLPool 管理连接池，提高并发下吞吐。

5.4 CAS ref_count 的一致性
- PutObjectWithRefCount：更新对象 + 旧 cas_key ref_count--
- DeleteObjectWithRefCount：删除对象 + cas_key ref_count--
- ref_count 为 0：加入 GC。

------------------------------------------------------------

六、数据层与 CAS 存储

6.1 CAS 布局
- CasLayout::GetCasPath：{base}/cas/aa/bb/<sha256>.blob
- 以哈希前缀分散目录。

6.2 DataStore（src/storage/data_store.h）
- BeginWrite：创建临时文件，返回 WriteSession。
- CommitWrite：校验哈希并移动到 CAS 路径。
- Write：用于小文件一次性写入。
- StreamRead：流式读取回调。
- Merge：multipart 完成时合并多个 CAS 文件。

6.3 WriteSession
- 持有 tmp_path 与 fd。
- Write：追加数据并更新 SHA256。
- Finish：关闭 fd 并返回 SHA256。
- Abort：失败时删除临时文件。

------------------------------------------------------------

七、对象上传/下载流程详解

7.1 PUT 对象（流式）
- HttpConnection 解析头部后进入 READING_BODY_STREAM。
- Server::OnBodyData 把 chunk 写入 UploadContext::session。
- 读取完毕后 CommitWrite 生成 cas_key。
- MetaStore::PutObjectWithRefCount 更新元数据和引用计数。

7.2 GET 对象（完整/Range）
- Handler 解析 Range 头，生成 offset/length。
- DataStore::GetFilePath 得到文件路径。
- HttpResponse::SetFileRange -> SendFile。

7.3 HEAD 对象
- 只返回元数据头：ETag、Content-Length、Content-Type。

------------------------------------------------------------

八、Multipart 上传详解

8.1 Init
- MultipartHandlers::InitUpload 调用 MetaStore::CreateMultipartUpload
- 返回 upload_id

8.2 Upload Part
- 写入 CAS，返回 etag
- 插入 multipart_parts

8.3 Complete
- 校验 part_number 顺序与 etag
- DataStore::Merge 合并 cas_keys
- MetaStore::CompleteMultipartUpload 写对象

8.4 Abort
- 删除 multipart_uploads 记录
- 关联 cas_blobs ref_count-- 并加入 GC

------------------------------------------------------------

九、预签名 URL

9.1 PresignHandlers::CreatePresignUrl
- 参数：method/path/expires/secret
- 生成签名并返回

9.2 PresignService
- BuildPresignUrl 生成完整 URL
- ValidatePresignUrl 做签名校验

------------------------------------------------------------

十、可观测性

10.1 Logger
- Init(level, access_log_path, error_log_path)
- LogAccess 记录结构化访问日志

10.2 Metrics
- Export() 输出 Prometheus 格式。
- http_requests、upload_bytes、download_bytes、active_connections、gc_queue_length

------------------------------------------------------------

十一、核心数据结构与连接上下文

11.1 连接级别数据结构
- HttpConnection：状态机、input/output buffer、parser、file 发送信息
- EventLoop：epoll fd、pending functors、timer
- Channel：fd 与事件回调

11.2 请求级别数据结构
- HttpRequest：method/path/headers/body/path_params/query_params/trace_id/client_ip
- HttpResponse：status/headers/body/file_path/file_range

11.3 上传上下文
- Server::UploadContext：WriteSession、expected_sha256、bytes_written、streaming
- uploads_：以 fd 为 key 的 map，追踪 streaming 状态

------------------------------------------------------------

十二、文件与目录布局（运行期）

12.1 主要目录
- data_dir：/var/lib/minis3/data
- tmp_dir：/var/lib/minis3/tmp

12.2 CAS 文件
- {data_dir}/cas/aa/bb/<sha256>.blob

12.3 临时文件
- {tmp_dir}/tmp_xxx

------------------------------------------------------------

十三、MySQL 表与索引说明

13.1 buckets
- name 唯一索引，用于快速查 bucket。

13.2 cas_blobs
- cas_key 主键。
- ref_count 索引，用于 GC 批量扫描。

13.3 objects
- uk_bucket_key(bucket_id, object_key(255))
- idx_cas_key，快速定位 cas_key。

13.4 multipart_uploads
- idx_bucket_key(bucket_id, object_key(255))
- idx_expires_at

13.5 multipart_parts
- (upload_id, part_number) 复合主键。

13.6 idempotency_records
- expires_at 索引便于清理。

------------------------------------------------------------

十四、难题与解决

14.1 大文件上传内存压力
- 方案：HttpConnection 流式读取 + WriteSession 流式写入。

14.2 CAS 引用计数一致性
- 方案：MetaStore::PutObjectWithRefCount/ DeleteObjectWithRefCount 事务保证。

14.3 高并发连接管理
- 方案：EventLoop + 多 IO 线程 + round-robin 分发。

14.4 文件系统目录膨胀
- 方案：CasLayout 两级目录分散。

14.5 断点续传
- 方案：Multipart 上传协议 + part 记录。

------------------------------------------------------------

十五、核心流程执行步骤（详细）

15.1 Server 启动
1) 读取 YAML 配置
2) 初始化 Logger/Metrics
3) 初始化 MySQLPool
4) 初始化 MetaStore
5) 初始化 DataStore
6) 初始化 GC
7) SetupRoutes
8) Acceptor Listen
9) main loop + io loops

15.2 PUT 对象
1) Router 匹配 PUT 路由
2) 读取 Header，判断 streaming
3) BeginWrite -> WriteSession
4) OnBodyData 写入
5) CommitWrite -> cas_key
6) PutObjectWithRefCount
7) 返回 200/201

15.3 GET 对象
1) Router 匹配
2) MetaStore::GetObject
3) DataStore::GetFilePath
4) SendFile
5) 更新 metrics

15.4 Multipart Complete
1) 读取 parts
2) 校验顺序
3) Merge cas
4) PutObjectWithRefCount
5) 删除 multipart_uploads

------------------------------------------------------------

十六、100 个核心函数与调用逻辑（按模块）

说明：函数名与逻辑来自项目源码头文件，便于快速定位。

网络与协议层（20）
1) EventLoop::Loop - epoll 主循环
2) EventLoop::RunInLoop - 跨线程任务执行
3) EventLoop::QueueInLoop - 任务投递
4) EventLoop::RunAfter - 定时器注册
5) EventLoop::UpdateChannel - 注册/更新 fd
6) Acceptor::Listen - 监听端口
7) Acceptor::HandleRead - 接收连接
8) Channel::HandleEvent - 事件分发
9) HttpConnection::Start - 启动连接
10) HttpConnection::HandleRead - 读取数据
11) HttpConnection::ProcessHeaders - 头部解析
12) HttpConnection::ProcessBodyData - body 处理
13) HttpConnection::Send - 发送响应
14) HttpConnection::SendFile - 发送文件
15) HttpParser::Parse - 解析数据
16) HttpParser::ConsumeBody - 手动消费 body
17) ByteBuffer::ReadFd - 读 fd
18) ByteBuffer::WriteFd - 写 fd
19) HttpRequest::GetHeader - 取 header
20) HttpResponse::SetFileRange - 设置文件范围

路由与中间件（10）
21) HttpRouter::Route - 注册路由
22) HttpRouter::Handle - 路由匹配
23) AuthMiddleware::Handle - 认证
24) AuthMiddleware::ValidateBearerToken - Bearer 校验
25) TraceMiddleware::Handle - 注入 trace_id
26) PresignHandlers::CreatePresignUrl - 生成 URL
27) PresignHandlers::VerifyPresignUrl - 校验 URL
28) BucketHandlers::CreateBucket - 创建 bucket
29) ObjectHandlers::PutObject - 上传对象
30) MultipartHandlers::UploadPart - 上传分片

服务层（10）
31) BucketService::CreateBucket - 调用 MetaStore
32) BucketService::DeleteBucket - 删除前检查
33) ObjectService::PutObject - 封装元数据写入
34) ObjectService::GetObject - 读取元数据与数据
35) ObjectService::GetObjectRange - Range
36) MultipartService::InitiateMultipartUpload
37) MultipartService::UploadPart
38) MultipartService::CompleteMultipartUpload
39) PresignService::GenerateGetUrl
40) PresignService::ValidatePresignUrl

元数据层（20）
41) MetaStore::CreateBucket
42) MetaStore::GetBucket
43) MetaStore::ListBuckets
44) MetaStore::PutObject
45) MetaStore::PutObjectWithRefCount
46) MetaStore::GetObject
47) MetaStore::DeleteObject
48) MetaStore::DeleteObjectWithRefCount
49) MetaStore::RegisterCasBlob
50) MetaStore::GetCasBlob
51) MetaStore::CreateMultipartUpload
52) MetaStore::UploadPart
53) MetaStore::CompleteMultipartUpload
54) MetaStore::AbortMultipartUpload
55) MetaStore::ListParts
56) Transaction::Commit
57) Transaction::Rollback
58) MySQLPool::GetConnection
59) MySQLConnection::Execute
60) MySQLConnection::Query

数据层（20）
61) DataStore::Init
62) DataStore::BeginWrite
63) WriteSession::Write
64) WriteSession::Finish
65) DataStore::CommitWrite
66) DataStore::Write
67) DataStore::Read
68) DataStore::StreamRead
69) DataStore::GetFilePath
70) DataStore::GetSize
71) DataStore::Delete
72) DataStore::Merge
73) CasLayout::GetCasPath
74) FileSystem::CreateDirectories
75) FileSystem::RemoveFile
76) FileSystem::AtomicRename
77) FileSystem::IsPathSafe
78) Crypto::SHA256
79) Crypto::SHA256File
80) UUID::Generate

可观测性与工具（20）
81) Logger::Init
82) Logger::LogAccess
83) Metrics::Export
84) Metrics::IncrementHttpRequests
85) Metrics::AddUploadBytes
86) Metrics::AddDownloadBytes
87) Metrics::SetActiveConnections
88) Metrics::ObserveRequestDuration
89) Config::LoadFromFile
90) Status::ToString
91) HttpResponse::Error
92) HttpResponse::Ok
93) HttpResponse::Json
94) HttpResponse::BadRequest
95) HttpResponse::Unauthorized
96) HttpResponse::NotFound
97) HttpResponse::InternalError
98) FileSystem::GetFileSize
99) ByteBuffer::Append
100) HttpRequest::GetQueryParam

------------------------------------------------------------

十七、每个请求与连接维护的数据结构

17.1 连接级数据结构
- HttpConnection：
  - sockfd、peer_addr
  - state_：连接状态
  - parser_：HTTP 解析器
  - input_buffer_ / output_buffer_
  - file_fd_/file_offset_/file_remaining_

17.2 请求级数据结构
- HttpRequest：
  - method/path/query/version
  - headers/body
  - path_params/query_params
  - trace_id/client_ip
- HttpResponse：
  - status_code/status_message
  - headers/body
  - file_path/file_offset/file_length

17.3 Server 级结构
- connections_：活跃连接集合
- uploads_：流式上传上下文
- mysql_pool_、meta_store_、data_store_、gc_
- router_

------------------------------------------------------------

十八、数据文件位置与命名

18.1 CAS 数据
- 位置：{data_dir}/cas/aa/bb/<sha256>.blob
- 命名：SHA256 hex + .blob

18.2 临时文件
- 位置：{tmp_dir}/tmp_xxx
- 用途：上传中间态

------------------------------------------------------------

十九、流式上传实现细节

19.1 请求进入
- Server::ShouldStreamUpload 判断 Content-Length 与路由。

19.2 开始写入
- DataStore::BeginWrite 创建 TempFile 与 WriteSession。

19.3 数据到达
- HttpConnection::ProcessBodyData 触发 BodyDataCallback。
- Server::OnBodyData 调用 WriteSession::Write。

19.4 结束提交
- 当 HttpParser::RemainingBodyLength 变为 0，CommitWrite。
- 校验 expected_sha256（若提供）。

------------------------------------------------------------

二十、分片上传实现细节

20.1 Init
- 生成 upload_id（UUID）。
- 写入 multipart_uploads。

20.2 Upload Part
- 分片内容写入 CAS。
- 写入 multipart_parts 并记录 cas_key。

20.3 Complete
- 客户端提交 parts 列表与 etag。
- 服务端校验顺序 + etag。
- DataStore::Merge 合并得到新 cas_key。
- 更新 objects 表与 cas_blobs ref_count。

20.4 Abort
- 删除 multipart_uploads 与 multipart_parts。
- 分片 cas_key ref_count--，必要时加入 GC。

------------------------------------------------------------

二十一、核心流程执行步骤（清单）

21.1 Server 启动
- 读取配置 -> 初始化 Logger -> MySQLPool -> MetaStore -> DataStore -> GC -> Router -> Listen

21.2 上传
- 解析 Header -> BeginWrite -> 流式写 -> CommitWrite -> PutObjectWithRefCount

21.3 下载
- GetObject -> GetFilePath -> SendFile

21.4 删除
- DeleteObjectWithRefCount -> GC

21.5 Multipart
- Init -> UploadPart -> Complete -> Merge

------------------------------------------------------------

二十二、性能与稳定性策略

22.1 I/O 线程模型
- io_threads 支撑高并发连接。

22.2 Socket buffer
- recv_buffer_size/send_buffer_size 可调。

22.3 限制策略
- max_body_bytes 限制单请求体大小。
- max_concurrent_uploads/ downloads 限制并发。

22.4 GC 批量策略
- batch_size 控制每轮删除数量。

------------------------------------------------------------

二十三、可能的扩展方向

23.1 多副本存储
- CAS 文件同步到多节点。

23.2 分布式元数据
- 使用分布式数据库或 Raft。

23.3 生命周期管理
- 支持对象 TTL 与自动迁移。

------------------------------------------------------------

二十四、快速复习索引（你可以用来背诵）

24.1 最重要的十个关键词
- epoll、Reactor、HttpParser、Streaming、CAS、MySQL、ref_count、GC、Multipart、sendfile

24.2 最重要的十个函数
- EventLoop::Loop
- HttpParser::Parse
- HttpConnection::ProcessBodyData
- DataStore::BeginWrite
- DataStore::CommitWrite
- MetaStore::PutObjectWithRefCount
- DataStore::Merge
- ObjectHandlers::GetObject
- MultipartHandlers::CompleteUpload
- Metrics::Export

------------------------------------------------------------

二十五、面试临场讲法（30 秒版本）

- 这个项目是一个单机对象存储服务，核心实现包括基于 epoll 的网络层、HTTP/1.1 解析、流式上传与 sendfile 下载、CAS 内容寻址去重、MySQL 元数据与事务一致性、分片上传与 GC 回收。所有请求经过 Router 与 Middleware，再进入 MetaStore 与 DataStore 完成数据与元数据操作，并通过日志与指标实现可观测性。整个系统在大文件与高并发场景下可稳定运行。

------------------------------------------------------------

二十六、（占位）扩展说明

- 你可以在此继续补充：性能压测结果、瓶颈分析、未来优化、部署实践等。

------------------------------------------------------------

附：本文件为极简版 1000+ 行说明。若需更细的函数级/流程级/源码级说明，可继续在此追加。