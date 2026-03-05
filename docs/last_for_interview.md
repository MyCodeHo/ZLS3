本文件汇总 MiniS3-CPP 项目实现的关键细节，便于面试与后续开发参考。

一、总体架构概览
- 网络层：自研 epoll Reactor（Acceptor、EventLoop、Channel），HTTP 状态机与流式解析（src/net/epoll、src/net/http）。
- 路由/中间件：HttpRouter + Auth/Trace 中间件（src/api/*）。
- 服务层：Bucket/Object/Multipart/Presign 等 Service（src/service/*）。
- 元数据层：MetaStore（MySQL）封装事务与表操作（src/db/meta_store.*）。
- 存储层：DataStore（CAS 内容寻址）、CasLayout、临时写入、Merge、GC（src/storage/*）。
- 背景任务：GC、multipart 过期清理、指标导出。

二、数据库表与字段含义（对应 scripts/mysql_init.sql）
- `buckets`：桶元数据（`id`, `name`(UNIQUE), `created_at`）。桶名在 DB 层为全局唯一。
- `cas_blobs`：CAS 物理内容元信息（`cas_key`(SHA256), `size`, `ref_count`, `created_at`, `updated_at`），供去重与 GC。
- `objects`：逻辑对象元数据（`id`, `bucket_id`, `object_key`, `cas_key`, `size`, `content_type`, `etag`, timestamps），`UNIQUE (bucket_id, object_key(255))` 保证桶内名唯一。
- `multipart_uploads` / `multipart_parts`：分片会话与分片记录（upload_id、part_number、cas_key、size、etag）。
- `idempotency_records`：幂等记录（请求哈希 + 上次响应），防重复处理。
- `api_keys`：简单 API Key 存储（测试用 key 已插入）。

三、重要实现流程
1) 上传（PUT 单对象，流式）
	- `Server` 在接收请求后创建 `WriteSession`（`DataStore::BeginWrite` 在 `tmp_dir/upload/` 创建 UUID.part）。
	- `HttpConnection` 在 READING_BODY_STREAM 状态把 body chunk 交给 `WriteSession::Write`，同时增量更新 SHA256 与 bytes_written。
	- 写完后 `WriteSession::Finish` 做 `fsync` 并返回 SHA256（cas_key）；`DataStore::CommitWrite` 检查去重、创建目录并 `AtomicRename(tmp->final)` 提交到 `{data_dir}/cas/aa/bb/<sha256>.blob`。
	- `MetaStore::PutObjectWithRefCount` 在事务内执行：新 cas `INSERT ... ON DUPLICATE KEY UPDATE ref_count=ref_count+1`，旧 cas `ref_count--`，并 `INSERT/ON DUPLICATE UPDATE` objects；若旧 cas 变为 0 则返回 GC 候选。

2) 下载（GET）
	- Handler 调用 `MetaStore::GetObject(bucket_id, object_key)` 得到 `cas_key`、size、etag 等。
	- `DataStore::GetFilePath(cas_key)` 映射物理路径，`HttpConnection::SendFile` 使用 `sendfile` 零拷贝发送，支持 Range。

3) Multipart（分片上传）
	- Init：`multipart_uploads` 插入 upload_id（UUID）。
	- UploadPart：每个 part 写入临时文件 -> CommitWrite 得到 part 的 cas_key -> 在 `multipart_parts` 插入记录（可同时维护 cas_blobs.ref_count）。
	- Complete：按 parts 顺序校验并 `DataStore::Merge` 合并为新临时文件 -> CommitWrite 得到 new cas_key -> 在事务中更新 objects 与 cas_blobs 的引用计数。
	- Abort：删除 multipart 记录并递减相关 cas_blobs 引用。

4) GC（垃圾回收）
	- `GarbageCollector` 后台线程周期性或按队列处理 `ref_count==0` 的 cas_blobs：调用 `DataStore::Delete` 删除物理文件并在 DB 中删除记录。

四、关键实现与注意点
- 临时文件实践：临时文件写入位于 `tmp_dir`（upload/、merge/、multipart/），使用 `UUID.part` 命名，写入过程中由 `WriteSession` 管理，未完成会在析构或 Abort 时删除。
- 原子提交：通过同文件系统内的 `::rename()`（封装为 `FileSystem::AtomicRename`）做到 O(1) 原子提交，避免两次全量拷贝；需保证 `tmp_dir` 与 `data_dir` 在同一挂载点，跨设备会导致 `rename` 失败（EXDEV）。
- 数据持久性：`WriteSession::Finish` 做 `fsync(fd)`，建议在需要更强持久性时对目标目录做 `fsync`。
- 去重与引用计数：写入成功后在 DB 中 `INSERT/ON DUPLICATE` cas_blobs 改变 ref_count；覆盖旧对象在同一事务内对旧 cas 递减，保证元数据与物理数据一致性。
- 锁与并发：`PutObjectWithRefCount` 使用 `SELECT ... FOR UPDATE` 读取旧 cas_key 以避免竞态；EventLoop 仅负责网络和解析，阻塞 IO/DB 操作应下沉到 worker 避免阻塞 epoll。
- 目录布局：`CasLayout::GetCasPath(cas_key)` 使用哈希前缀两级（aa/bb）分散目录以防单目录膨胀。

五、权限与鉴权
- AuthMiddleware 基于 `Authorization: Bearer` 或 `X-API-Key` 校验请求；当前实现以 `api_keys` 表为凭据来源（简单字符串校验）。
- 桶（bucket）在 DB 层无 owner 字段（当前 schema 仅含 name），因此项目当前并不基于用户隔离桶所有权（所有 token 共享访问测试桶）；如需多租户需扩展 buckets 表并在 `CreateBucket`/`GetBucket` 中按照 owner 校验。

六、表之间协同（上传/下载/删除总结）
- 上传：DataStore 写临时文件 -> CommitWrite 得到 cas_key -> MetaStore 事务性地更新 cas_blobs/ref_count 与 objects（覆盖则调整旧 ref_count）-> GC 可能回收旧 cas。
- 下载：Handler -> MetaStore::GetObject -> DataStore 映射 cas_key -> 读取文件并返回。
- 删除：MetaStore::DeleteObjectWithRefCount 在事务中删除 objects 并对 cas_blobs.ref_count--，若为 0 则交给 GC 删除物理文件。

七、源码位置速查
- 入口：`cmd/minis3_server/main.cpp`
- 网络/HTTP：`src/net/epoll/*`, `src/net/http/*`, `src/net/buffer/*`
- API/Handlers：`src/api/handlers/*`, `src/api/middleware/*`
- 服务层：`src/service/*`
- 元数据：`src/db/meta_store.*`, `src/db/mysql_pool.*`, `src/db/mysql_tx.*`
- 存储：`src/storage/data_store.*`, `src/storage/cas_layout.*`, `src/storage/gc.*`
- 工具：`src/util/*`
- 初始化脚本：`scripts/mysql_init.sql`

八、建议与优化方向（供后续改进）
- 确保 `tmp_dir` 与 `data_dir` 在同一分区，避免 EXDEV 导致的隐式拷贝。
- 若支持多租户，应在 `buckets` 表加入 `owner_id` 并将 `buckets.name` 改为在 owner 作用域内唯一（或保留全局唯一并设计命名策略）。
- 对耗时磁盘/DB 操作使用 worker 池异步处理，避免阻塞 EventLoop。
- 分片合并可尝试使用 `copy_file_range`、reflink 或内核提供的零拷贝机制减少 IO。

—— End of summary ——

