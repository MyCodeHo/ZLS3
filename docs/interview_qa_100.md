# MiniS3-CPP 面试问答（100 题，含参考答案）

Q1：这个项目解决了什么问题？
A1：它提供一个单机对象存储服务，实现类 S3 的核心能力，包括 Bucket 管理、对象上传/下载、Range 下载、分片上传、预签名 URL、幂等处理、CAS 去重与 GC 回收，强调高并发与大文件场景下的稳定性。

Q2：为什么选用 C++20 实现？
A2：C++ 适合高性能网络服务，能够控制内存与零拷贝路径；C++20 提供更好的语言特性与标准库支持，降低工程复杂度。

Q3：项目的整体架构分层是什么？
A3：分为网络与协议层（epoll + HTTP 解析）、API/路由层（Handler + Middleware）、服务与存储层（MetaStore/MySQL + DataStore/CAS + GC）、监控与运维层（日志 + Prometheus + 脚本）。

Q4：网络模型使用的是什么？
A4：Reactor 模型，基于 epoll 事件循环。Acceptor 接收连接并分发到 I/O 线程的 EventLoop，由 Channel 负责事件回调。

Q5：为什么需要 ByteBuffer？
A5：ByteBuffer 支持高效的读写与空间复用，减少频繁分配，配合 readv/writev 提升 I/O 吞吐。

Q6：HTTP 解析是如何做流式处理的？
A6：HttpParser 按请求行、头部、Body 三阶段解析；对于大 Body，连接进入 READING_BODY_STREAM 状态，边读边处理，不将内容全部载入内存。

Q7：如何判断是否进入流式上传？
A7：通过配置与请求特征（如 Content-Length 或路由类型）在 Server::ShouldStreamUpload 中判断，决定走 Streaming 路径。

Q8：流式上传的关键数据结构是什么？
A8：HttpConnection 的状态机 + Server 的 UploadContext（WriteSession、expected_sha256、bytes_written、streaming）。

Q9：WriteSession 的作用？
A9：封装临时文件写入与 SHA256 流式计算，Finish 后得到 CAS key；Abort 用于失败回滚。

Q10：CAS 内容寻址的好处？
A10：用 SHA256 作为文件地址，实现天然去重；同内容共享一份数据，降低存储占用并简化一致性。

Q11：CAS 文件存储路径怎么设计？
A11：CasLayout 将哈希分层目录组织，路径格式为 {base}/cas/aa/bb/<sha256>.blob，避免单目录文件过多。

Q12：对象元数据和数据如何分离？
A12：元数据存储在 MySQL（objects 表），数据存储在文件系统 CAS 目录；二者通过 cas_key 关联。

Q13：如何实现 Range 下载？
A13：解析 Range 头（ObjectHandlers::ParseRange），校验范围后通过 HttpResponse::SetFileRange 走 sendfile 发送指定字节段。

Q14：sendfile 的收益是什么？
A14：减少用户态拷贝与上下文切换，提升大文件下载吞吐并降低 CPU 消耗。

Q15：对象删除时如何处理 CAS 引用？
A15：MetaStore 提供 DeleteObjectWithRefCount，在事务内更新对象与 cas_blobs.ref_count；若 ref_count 为 0，加入 GC 队列删除。

Q16：GC 具体怎么工作？
A16：GarbageCollector 在后台线程周期运行，批量取出待删除 cas_key，删除文件并回调更新元数据。

Q17：幂等性怎么实现？
A17：MySQL 的 idempotency_records 表记录 idempotency_key 与 request_hash/response；重复请求可直接返回已记录的响应。

Q18：项目中认证方式有哪些？
A18：静态 Bearer Token、API Key（api_keys 表）、以及预签名 URL。

Q19：AuthMiddleware 主要做什么？
A19：对需要鉴权的路径进行认证，支持 Bearer Token 与 API Key；可扩展到 Presign 校验。

Q20：TraceMiddleware 的作用？
A20：为请求生成 trace_id 并写入 HttpRequest，贯穿日志与错误响应，便于排障。

Q21：HttpRouter 如何实现参数路由？
A21：RouteEntry 中保存正则与参数名列表，通过 pattern 编译成 regex；匹配后填充到 HttpRequest 的 PathParams。

Q22：HttpResponse 与 HttpRequest 的关系？
A22：HttpRequest 保存输入上下文（方法、路径、头、Body）；HttpResponse 提供状态码、头和 Body 以及文件发送信息。

Q23：连接复用如何实现？
A23：HttpParser 支持 Reset；HttpConnection 在 Keep-Alive 下复用连接并重置解析器。

Q24：空闲连接如何处理？
A24：HttpConnection 支持 idle timer，超时自动 Close；EventLoop 提供定时器机制。

Q25：EventLoop 的核心职责？
A25：epoll_wait 轮询事件，分发到 Channel 回调，同时处理跨线程任务与定时器。

Q26：Channel 的核心作用？
A26：将 fd 与读写事件回调绑定，控制读写事件的启用/关闭，并接收 epoll 触发。

Q27：Acceptor 的功能？
A27：管理 listen socket，接受新连接并回调给 Server::OnNewConnection。

Q28：MySQLPool 如何避免频繁建连？
A28：连接池维持固定数量连接，GetConnection 获取可用连接，连接使用结束自动归还。

Q29：Transaction 的作用？
A29：RAII 事务封装，失败回滚，成功提交，保证元数据与 CAS 引用计数一致性。

Q30：MetaStore 的职责？
A30：抽象数据库访问，负责 bucket/object/cas/multipart/idempotency 等 CRUD 与事务逻辑。

Q31：DataStore 的职责？
A31：负责 CAS 数据写入、读取、合并与删除，提供流式写入与 sendfile 路径。

Q32：Multipart Upload 的核心流程？
A32：Init 生成 upload_id；UploadPart 将分片写入 CAS 并记录元数据；Complete 按顺序合并分片并生成新对象元数据。

Q33：如何校验分片上传的顺序和完整性？
A33：CompleteUpload 需要客户端提供 part 列表与 etag，服务端按 part_number 校验并合并。

Q34：Multipart 数据存储在哪些表？
A34：multipart_uploads 与 multipart_parts 表记录会话与分片元数据，分片对应 cas_blobs。

Q35：对象列表如何实现？
A35：MetaStore::ListObjects 支持 prefix、delimiter、start_after 与分页，实现类似 S3 的列举语义。

Q36：如何处理大文件上传内存占用？
A36：使用流式写入，边读边写临时文件，同时流式计算 SHA256，避免完整 Body 常驻内存。

Q37：如何避免路径遍历问题？
A37：FileSystem::IsPathSafe 校验路径，结合 CasLayout 的固定规则生成文件路径。

Q38：配置系统怎么加载？
A38：Config::LoadFromFile 解析 YAML，生成 server/storage/mysql/auth/limits/multipart/log/metrics/gc 配置结构。

Q39：日志有哪些输出？
A39：Logger 提供访问日志与错误日志；可选 JSON 格式并输出 trace_id、方法、路径、状态、延迟等字段。

Q40：Metrics 指标有哪些？
A40：HTTP 请求计数、上传/下载字节、活跃连接数、存储用量、对象数、CAS blob 数、GC 队列长度、请求延迟直方图。

Q41：如何统计请求耗时？
A41：在请求处理入口记录开始时间，响应后调用 Metrics::ObserveRequestDuration 和 Logging::AccessLog。

Q42：为什么需要 Idempotency？
A42：避免重复请求导致多次写入或异常，尤其在网络重试场景下保证一致性。

Q43：对象更新时 CAS 引用计数如何维护？
A43：MetaStore::PutObjectWithRefCount 在事务中插入/更新对象并调整 cas_blobs.ref_count。

Q44：对象删除后如何更新统计？
A44：GC 删除成功后调用回调更新元数据，并通过 Metrics 更新对象数与存储用量。

Q45：如何避免单目录过多文件？
A45：CasLayout 使用两级目录分桶（aa/bb），均衡目录数量。

Q46：如何处理 MySQL 连接不可用？
A46：MySQLConnection::Ping 与连接池超时控制；错误通过 Status 返回并转为 HTTP 错误响应。

Q47：请求体过大如何处理？
A47：HttpConnection 依据 limits.max_body_bytes 限制 Content-Length，超限返回 413。

Q48：如何处理 Range 不合法？
A48：ParseRange 返回空或非法时返回 416，并带上 Content-Range 指定文件长度。

Q49：如何实现健康检查？
A49：HealthHandlers::HealthCheck/ReadyCheck 返回基础状态；readyz 可扩展为 MySQL/磁盘检查。

Q50：如何输出 Prometheus 指标？
A50：HealthHandlers::Metrics 调用 Metrics::Export 生成文本响应。

Q51：HttpResponse 的文件发送如何触发？
A51：若设置 FilePath，HttpConnection::SendFile 进入 SENDING_FILE 状态并使用 sendfile。

Q52：HTTP Keep-Alive 如何处理？
A52：HttpRequest::IsKeepAlive 和 HttpResponse::SetKeepAlive 控制连接复用。

Q53：如何解决上传过程的断点恢复？
A53：通过 Multipart 上传机制，客户端记录 upload_id 和已完成 part 以实现恢复。

Q54：如何生成预签名 URL？
A54：PresignHandlers/CreatePresignUrl 生成签名参数；PresignService 组合 base_url 与签名。

Q55：如何验证预签名 URL？
A55：AuthService/PresignService 根据 method、path、expires、signature 校验 HMAC。

Q56：为什么需要 tmp_dir？
A56：流式写入先落临时文件，校验 SHA256 后再移动到 CAS 目录，避免半写文件污染。

Q57：CAS 文件命名规则是什么？
A57：以 SHA256 hex 为文件名，后缀 .blob，路径由 CasLayout 计算。

Q58：请求中的 TraceId 在哪里设置？
A58：TraceMiddleware 生成并写入 HttpRequest，后续日志与错误响应使用。

Q59：为什么使用 MySQL 而不是 KV？
A59：MySQL 支持事务与复杂查询，适合对象列表与多表一致性维护。

Q60：分片上传完成时如何合并？
A60：DataStore::Merge 按 part 顺序读取各 CAS 文件并写入新 CAS 文件，生成新的 cas_key。

Q61：DeleteBucket 需要哪些前置检查？
A61：MetaStore::IsBucketEmpty 确保桶内无对象，再执行删除。

Q62：ListObjects 如何实现分页？
A62：使用 start_after 或 marker 作为游标，配合 max_keys，返回 is_truncated 与 next key。

Q63：对象复制如何实现？
A63：ObjectService::CopyObject 读取源对象元数据与 cas_key，增加 ref_count 并写入目标对象。

Q64：HTTP 解析器如何处理错误？
A64：HttpParser 在解析失败时设置 ERROR 状态并给出 error_message。

Q65：如何限制 Header 过大？
A65：HttpParser 设置 kMaxHeaderSize，超过则报错。

Q66：如何处理非法 method？
A66：StringToMethod 返回 UNKNOWN，Router 未匹配时返回 404 或 405。

Q67：请求体没有 Content-Length 怎么办？
A67：若不支持 chunked，会返回 MissingContentLength 或 400。

Q68：Chunked 是否支持？
A68：HttpParser 提供 IsChunked 标记，但实际处理是否完整支持需结合实现细节说明。

Q69：日志结构化字段有哪些？
A69：trace_id、client_ip、method、path、status、latency、bytes_in/out、bucket、object。

Q70：如何处理并发连接统计？
A70：Metrics 的 active_connections 原子计数，连接建立/关闭时增减。

Q71：为什么要做 GC？
A71：对象删除后 CAS 文件可能无人引用，需要回收节省空间。

Q72：GC 与 MetaStore 一致性如何保证？
A72：DeleteObjectWithRefCount 在事务内计算是否为 0；GC 删除后回调更新元数据。

Q73：如何处理 MySQL 错误？
A73：MySQLConnection::LastError 返回错误字符串，上层转为 Status 并映射成 HTTP 5xx。

Q74：流式上传如何校验 SHA256？
A74：WriteSession 在写入时更新 SHA256Context，Finish 后比对 expected_sha256。

Q75：对象的 ETag 是什么？
A75：通常使用内容哈希或上传摘要，响应中返回，客户端可用于一致性校验。

Q76：为什么要有 limits 配置？
A76：限制请求体、Header、并发上传/下载与超时，避免资源耗尽。

Q77：如果请求超时如何处理？
A77：通过连接 idle timeout 和 request_timeout 配置；超时后强制关闭。

Q78：Metrics 导出是否线程安全？
A78：Metrics 内部使用 mutex 与 atomic 保护计数与直方图。

Q79：系统如何启动？
A79：main.cpp 解析配置，初始化日志、存储、数据库与 Server，然后 Start 进入事件循环。

Q80：如何进行功能测试？
A80：使用 scripts/test_api.sh 与 scripts/test_multipart.sh 发起端到端请求。

Q81：如何进行性能测试？
A81：使用 bench_upload.sh 与 bench_download.sh 并发压测。

Q82：如何部署？
A82：通过 Docker Compose 启动 MySQL 与服务，或本地编译运行。

Q83：为什么需要 MySQL 连接池？
A83：减少连接建立成本，提高并发下的吞吐。

Q84：HttpResponse 的 Json 响应怎么生成？
A84：SetJsonBody 设置 Content-Type 为 application/json 并写入 body。

Q85：API 路由如何处理 404？
A85：HttpRouter::SetNotFoundHandler 设置默认 404 响应。

Q86：如何防止 upload_id 被滥用？
A86：upload_id 是 UUID，结合鉴权与过期时间控制有效期。

Q87：Multipart 过期如何处理？
A87：MetaStore 记录 expires_at，清理策略可在 GC 或定时任务中执行。

Q88：对象 HEAD 返回哪些字段？
A88：ETag、Content-Length、Content-Type、Last-Modified 等元数据。

Q89：如何处理静态 token？
A89：在配置中定义 static_tokens，AuthMiddleware 校验 Authorization 头。

Q90：如何记录请求的 bytes_in/out？
A90：上传过程中统计写入字节，下载时根据 file_size/length 计入 Metrics。

Q91：CAS 引用计数为负怎么办？
A91：通过事务与约束保证不出现负值，异常时记录错误并阻断删除。

Q92：是否支持对象版本？
A92：MetaStore 的 ObjectInfo 包含 version_id/is_latest 字段，可扩展版本化能力。

Q93：如何实现 Bucket 列表？
A93：MetaStore::ListBuckets 根据 owner_id 查询 buckets 表。

Q94：readyz 与 healthz 有何区别？
A94：healthz 仅返回进程存活；readyz 可检查依赖（如 MySQL、磁盘空间）。

Q95：如何优化上传吞吐？
A95：增大 socket buffer、提升 io_threads、使用顺序写入、避免频繁 fsync。

Q96：如何优化下载吞吐？
A96：使用 sendfile、增加 send_buffer_size、减少用户态拷贝。

Q97：如何扩展到多机？
A97：可将 DataStore 替换为分布式存储，MetaStore 替换为集中式数据库，并引入一致性协议。

Q98：项目最难的点是什么？
A98：在单机架构下实现高并发与大文件流式处理，同时保证元数据一致性。

Q99：如果上传中断怎么处理？
A99：单次 PUT 可失败重试；对于大文件使用 Multipart 上传以恢复进度。

Q100：你从项目中学到了什么？
A100：掌握了网络事件循环、HTTP 解析、流式 I/O、CAS 存储、事务一致性、监控日志与工程化部署。
