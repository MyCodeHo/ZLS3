# 简历中的项目描述（可直接粘贴）

项目名称：MiniS3-CPP（C++20 单机对象存储服务）

项目定位：实现类 S3 的核心对象存储能力，覆盖 Bucket 管理、对象上传/下载、Range 下载、分片上传、预签名 URL、幂等处理、CAS 内容寻址、GC 垃圾回收、Prometheus 指标与结构化日志。

技术栈：C++20、Linux、epoll、MySQL(InnoDB)、OpenSSL、spdlog、yaml-cpp、nlohmann/json、CMake、Docker、Prometheus

个人职责与工作内容：
1. 设计并实现基于 epoll 的 Reactor 网络层，完成连接接入、事件分发与 HTTP/1.1 解析。
2. 完成对象上传/下载、Range 下载与流式处理，保证大文件不进内存。
3. 设计 CAS 内容寻址存储，使用 SHA256 做去重与引用计数。
4. 实现分片上传流程（init/upload/complete/abort），并在完成时合并 CAS 分片。
5. 搭建元数据层（MySQL），定义 buckets/objects/cas_blobs/multipart_* 等表结构与事务逻辑。
6. 实现 GC 机制，基于 ref_count 回收无引用的 CAS 文件。
7. 构建中间件体系：Trace、Auth、Presign 校验；统一错误与可观测性输出。
8. 维护配置系统与运行参数（I/O 线程、限制、存储路径、日志、指标、GC 等）。
9. 补齐测试与脚本（API 测试、分片测试、基准测试、快速启动）。

项目亮点（面试可讲）：
1. 完整实现对象存储“元数据 + 数据分离”的分层架构，业务清晰、可维护。
2. 通过 HttpParser + ByteBuffer 实现流式读取与零拷贝发送（sendfile），显著降低内存压力。
3. CAS + ref_count + GC 设计保证数据复用与回收的一致性。
4. Multipart 完成时合并分片并更新对象元数据，支持断点续传。
5. 对外提供 REST API，结合认证与预签名 URL，提高安全性与可用性。

可面试扩展点：
1. Reactor 与事件循环的线程模型，以及跨线程任务投递与定时器。
2. HTTP 解析与大文件流式处理的状态机设计。
3. MySQL 事务在对象替换与 GC 场景中的一致性保证。
4. CAS 布局与磁盘目录规划对性能的影响。
5. 监控指标与日志结构化设计的取舍。
