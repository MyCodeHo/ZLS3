# MiniS3 性能指南

## 性能指标

### 目标性能

基于设计文档的目标：

| 指标 | 目标值 |
|------|--------|
| PUT 小对象 (1KB) | ≥ 2000 ops/s |
| GET 小对象 (1KB) | ≥ 5000 ops/s |
| PUT 大对象 (100MB) | ≥ 200 MB/s |
| GET 大对象 (100MB) | ≥ 500 MB/s |
| P99 延迟 (小对象) | ≤ 50ms |

### 性能测试

使用提供的性能测试脚本：

```bash
# 生成测试文件
./scripts/gen_test_file.sh 10 test_10mb.bin
./scripts/gen_test_file.sh 100 test_100mb.bin

# 上传性能测试
./scripts/bench_upload.sh http://localhost:8080 default test_10mb.bin 10 100

# 下载性能测试  
./scripts/bench_download.sh http://localhost:8080 default test-object 10 100
```

### 实测结果（2026-03-05，单机本地）

测试环境（本次实测）：

- CPU：Intel i7-10700（16 逻辑核）
- 内存：15GiB
- 服务端：`build/minis3_server` + `configs/server.local.yaml`（`io_threads=2`，`worker_threads=4`）
- 元数据库：MySQL 8.0（Docker，127.0.0.1:3307）
- 存储目录：`/tmp/minis3/data`（位于本机磁盘）
- 网络：客户端与服务端同机 `localhost`

> 注：同机回环 + 页缓存会显著抬高 GET 吞吐，本节结果用于定位系统瓶颈，不等同生产吞吐承诺。

#### 1) 单请求延迟（串行 50 次）

| 场景 | P50 | P95 | P99 | 平均 |
|------|-----|-----|-----|------|
| PUT 1KB | 79.9ms | 255.3ms | 316.6ms | 96.2ms |
| GET 1KB | 0.76ms | 1.07ms | 1.11ms | 0.80ms |
| PUT 4KB | 88.5ms | 152.7ms | 389.8ms | 98.3ms |
| GET 4KB | 0.81ms | 1.12ms | 1.29ms | 0.80ms |
| PUT 1MB | 112.6ms | 225.1ms | 254.5ms | 125.8ms |
| GET 1MB | 1.18ms | 1.97ms | 2.01ms | 1.30ms |
| PUT 10MB | 1.167s | 1.335s | 1.434s | 1.202s |
| GET 10MB | 2.58ms | 2.91ms | 3.29ms | 2.60ms |
| PUT 100MB | 1.913s | 2.106s | 2.221s | 1.925s |
| GET 100MB | 19.81ms | 22.03ms | 22.79ms | 20.00ms |

#### 2) 吞吐/QPS（并发压测）

| 场景 | 并发 × 次数 | 成功数 | 总耗时 | QPS | 吞吐 |
|------|------------|--------|--------|-----|------|
| PUT 1KB | 20 × 200 | 200 | 22.387s | 8.93 | ~0.01 MB/s |
| GET 1KB | 20 × 200 | 200 | 0.122s | 1639.34 | ~1.60 MB/s |
| PUT 4KB | 20 × 200 | 199 | 25.071s | 7.93 | 0.03 MB/s |
| GET 4KB | 20 × 200 | 200 | 0.120s | 1666.66 | ~6.51 MB/s |
| PUT 1MB | 10 × 50 | 49 | 7.408s | 6.61 | 6.61 MB/s |
| GET 1MB | 10 × 50 | 50 | 0.040s | 1250.00 | ~1250 MB/s |
| PUT 10MB | 5 × 20 | 20 | 6.334s | 3.15 | 31.57 MB/s |
| GET 10MB | 5 × 20 | 20 | 0.036s | 555.55 | ~5555 MB/s |
| PUT 100MB | 2 × 5 | 5 | 8.409s | 0.59 | 59.46 MB/s |
| GET 100MB | 2 × 5 | 5 | 0.070s | 71.42 | ~7142 MB/s |

补充（串行 GET 吞吐，10 次下载）：

- GET 1MB：约 210.9 MB/s
- GET 10MB：约 1506.8 MB/s
- GET 100MB：约 4089.7 MB/s

#### 3) 冷缓存下载（复现）

测试流程：生成 100MB 文件 -> 上传到 `bench_cold_1` -> 清空页缓存 -> 下载一次并测量（使用仓库脚本）。

实测（2026-03-05，本机，root 清页缓存后执行）：

- 上传（单次，`bench_upload.sh`）: 总耗时 3.2075s，吞吐 ~31.17 MB/s（写入受磁盘限制）
- 冷缓存下载（`bench_download.sh default bench_cold_1 1 1`）: Total time = 0.027733715s，Throughput = 3605.71 MB/s

说明：尽管已执行 `echo 3 | sudo tee /proc/sys/vm/drop_caches`，下载仍然呈现远超物理盘的吞吐，说明同机回环、内核/驱动层缓存或 sendfile/零拷贝路径仍然可能使读取路径绕过慢盘瓶颈（或测量受其他缓存层影响）。建议在跨主机（client != server）和使用独立网络路径的场景下重复验证真实读取带宽。

补充（MySQL 查询基线，30 次）：

- `SELECT cas_key FROM objects ...`：P50 8.45ms，P95 9.18ms，平均 8.43ms

### 性能瓶颈分析（本次实测）

结论：当前瓶颈主要在 PUT 路径，而不是 GET 路径。

1. **小对象 PUT 被元数据路径主导（MySQL + 事务 + 往返）**

- 1KB/4KB PUT 延迟约 80~100ms，远大于数据写入本身所需时间。
- 同机 MySQL 单查询基线约 8~9ms，但 PUT 涉及多条 SQL（含事务、`FOR UPDATE`、`UPSERT`、`UPDATE`），叠加连接池/线程调度，整体放大到百毫秒级。

2. **大对象 PUT 受磁盘写入与 fsync 限制**

- 10MB PUT 吞吐约 31.57 MB/s，100MB PUT 吞吐约 59.46 MB/s。
- 项目写路径采用“临时文件写入 + `fsync` + `rename`”保证落盘一致性；在本机测试下，这一策略显著抬高了写入尾延迟。

3. **GET 明显受页缓存与回环网络加速，不代表真实磁盘读取能力**

- GET 10MB/100MB 的吞吐达到 GB/s 级，典型是 Linux page cache + `sendfile` 零拷贝路径效果。
- 该结果可证明读取路径开销较低，但不能用于估算跨机房/跨主机生产吞吐。

4. **并发升高时 PUT 扩展性较弱**

- 在并发 20~50 的小对象 PUT 压测下，QPS 仍在个位数到十位数，说明后端关键资源（DB 事务、锁竞争、磁盘同步写）先到瓶颈。

### 优化优先级建议

1. **优先优化小对象 PUT 的 DB 往返次数**

- 合并可合并的 SQL 交互，减少一次请求中的事务内语句数量。
- 评估把部分同步元数据更新改为异步批处理（需权衡一致性）。

2. **优化写入策略与介质**

- 将 `data_dir/tmp_dir` 固定到 NVMe SSD；避免慢盘对 `fsync` 放大影响。
- 在可接受的持久性策略下，评估 `fsync` 频率/批处理策略。

3. **压测方法分层**

- 增加“跨主机压测”和“冷缓存压测”（如测试前清页缓存或更换 key）以获取更接近生产的数据。
- 单独统计服务端处理时延（不含客户端排队时间），与端到端时延分开展示。

## 性能优化

## 磁盘基准测试（本机，dd + fio）

测试命令（在仓库根目录执行）：

```bash
# 顺序读写（dd，direct + fdatasync）
sync; time dd if=/dev/zero of=/tmp/minis3/dd_test bs=1M count=1024 oflag=direct conv=fdatasync
time dd if=/tmp/minis3/dd_test of=/dev/null bs=1M iflag=direct

# 随机读写（fio, 4k，iodepth=16）
fio --name=randwrite1 --filename=/tmp/minis3/fio_test --size=512M --bs=4k --rw=randwrite --iodepth=16 --direct=1 --numjobs=1 --time_based --runtime=10 --group_reporting
fio --name=randread1  --filename=/tmp/minis3/fio_test --size=512M --bs=4k --rw=randread  --iodepth=16 --direct=1 --numjobs=1 --time_based --runtime=10 --group_reporting
rm -f /tmp/minis3/fio_test /tmp/minis3/dd_test
```

实测结果（2026-03-05，本机）摘要：

- 顺序写（dd, 1GiB, direct+fdatasync）：约 179 MB/s
- 顺序读（dd, 1GiB, direct）：约 193 MB/s
- 随机写（fio, 4k, iodepth=16, 1 job）：约 1.08 MB/s（≈ 277 IOPS）
- 随机读（fio, 4k, iodepth=16, 1 job）：约 0.55 MB/s（≈ 138 IOPS）

说明：顺序吞吐 ~180–195 MB/s 与 `PUT` 路径中观测到的 ~31 MB/s 写入不同，后者受写入策略（fsync/rename）和服务器实现（临时文件 + 强制落盘）影响更大；随机 IOPS 非常低（百级 IOPS），这对大量小对象随机写场景有显著影响，尤其在使用旋转盘时。

## NVMe 实测（2026-03-05）

测试前提：

- 将 `configs/server.local.yaml` 的 `storage.data_dir/tmp_dir` 切到 NVMe 挂载：`/media/zpw/24804BB6804B8CEC/minis3_nvme/{data,tmp}`
- 设备信息：`nvme0n1`（`ROTA=0`）

### 1) 项目吞吐（MiniS3）

单次 100MB 对象：

- PUT（`bench_nvme_put_single`）：`TIME=1.359249s`，约 `77.14 MB/s`（`SPEED_Bps=77143775`）
- GET（`bench_nvme_100`）：`TIME=0.020309s`，约 `5163.11 MB/s`（`SPEED_Bps=5163109951`，同机缓存/回环特征明显）

并发压测（脚本）：

- `./scripts/bench_upload.sh ... 100MB 2x5`：`108.73 MB/s`（稳定一轮全成功）
- `./scripts/bench_download.sh ... bench_nvme_100 2x5`：`6765.47 MB/s`

### 2) NVMe 裸盘限制吞吐（dd + fio）

- 顺序写（dd, 1GiB, direct+fdatasync）：约 `2.2 GB/s`
- 顺序读（dd, 1GiB, direct）：约 `2.3 GB/s`
- 随机写（fio, 4k, iodepth=16, 1 job）：约 `200 MiB/s`（≈ `51.1k IOPS`）
- 随机读（fio, 4k, iodepth=16, 1 job）：约 `72.4 MiB/s`（≈ `18.5k IOPS`）

结论：切到 NVMe 后，项目 PUT 吞吐从此前机械盘量级显著提升（100MB 并发压测约到 `108.73 MB/s`），但仍明显低于 NVMe 裸盘上限，说明应用路径中的 `fsync + rename + 元数据事务` 仍是主要限制；GET 继续受同机缓存/回环加速，不能直接代表跨主机真实读盘吞吐。


### 系统配置

#### 文件描述符限制

```bash
# /etc/security/limits.conf
* soft nofile 65535
* hard nofile 65535
```

#### TCP 调优

```bash
# /etc/sysctl.conf
net.core.somaxconn = 65535
net.core.netdev_max_backlog = 65535
net.ipv4.tcp_max_syn_backlog = 65535
net.ipv4.tcp_fin_timeout = 10
net.ipv4.tcp_tw_reuse = 1
net.core.rmem_max = 16777216
net.core.wmem_max = 16777216
```

### 存储配置

#### SSD 优化

- 使用 SSD 存储数据目录
- 启用 TRIM：`mount -o discard`
- 使用 ext4 或 xfs 文件系统

#### 目录结构

CAS 使用两级目录哈希：

```
/data/cas/
  ab/
    cd/
      abcdef1234567890...  # SHA256 hash
```

这确保每个目录下的文件数量可控。

### MySQL 优化

#### InnoDB 配置

```ini
[mysqld]
innodb_buffer_pool_size = 2G      # 约 70% 物理内存
innodb_log_file_size = 256M
innodb_flush_log_at_trx_commit = 2  # 性能优先
innodb_flush_method = O_DIRECT
innodb_file_per_table = 1
```

#### 连接池

配置足够的连接池大小：

```yaml
mysql:
  pool_size: 20  # 根据并发数调整
```

### 应用配置

#### 工作线程

```yaml
server:
  io_threads: 4
  worker_threads: 8
```

#### 缓冲区

```yaml
server:
  recv_buffer_size: 1048576
  send_buffer_size: 1048576
```

## 监控指标

### Prometheus 指标

```
# HTTP 请求
minis3_http_requests_total{method,code}
minis3_http_request_duration_seconds_bucket{method,route,le}

# 流量
minis3_upload_bytes_total
minis3_download_bytes_total

# 连接与存储
minis3_active_connections
minis3_storage_used_bytes
minis3_objects_count
minis3_cas_blobs_count
minis3_gc_queue_length
```

### Grafana Dashboard

推荐监控面板：

1. **QPS 面板** - 请求速率
2. **延迟面板** - P50/P95/P99 延迟
3. **吞吐量面板** - MB/s
4. **错误率面板** - 4xx/5xx 比例
5. **连接数面板** - 活跃连接数
6. **数据库面板** - 查询延迟、连接数

## 容量规划

### 存储容量

```
总存储 = 对象数量 × 平均对象大小 × (1 + 开销系数)
```

开销系数约 1.05（元数据、索引等）

### 内存需求

```
内存 = MySQL Buffer Pool + 连接缓冲区 + 应用内存
     = 2GB + (连接数 × 64KB × 2) + 500MB
```

### IOPS 需求

```
PUT IOPS = PUT QPS × 2  # 写数据 + 写元数据
GET IOPS = GET QPS × 1  # 读数据
```

## 故障排查

### 常见性能问题

1. **高延迟**
   - 检查 MySQL 慢查询日志
   - 检查磁盘 I/O 使用率
   - 检查网络延迟

2. **低吞吐量**
   - 检查连接池是否满
   - 检查 CPU 使用率
   - 检查是否有锁竞争

3. **连接超时**
   - 增加连接池大小
   - 检查 MySQL max_connections
   - 检查网络连接限制

### 诊断命令

```bash
# 系统资源
top -H
iostat -x 1
vmstat 1

# 网络
ss -s
netstat -an | grep ESTABLISHED | wc -l

# MySQL
mysqladmin processlist
SHOW ENGINE INNODB STATUS
```

## 2026-03-07 优化记录：DataStore 落盘策略可配置

本次优化在存储层新增配置项：

```yaml
storage:
  sync_mode: "fsync"   # fsync | fdatasync | none
```

- `fsync`：最保守，数据和元数据都强制落盘（默认）。
- `fdatasync`：仅同步数据块与必要元数据，通常比 `fsync` 更轻。
- `none`：不主动同步，吞吐最高，但进程/机器异常时更容易丢最近写入。

### 基准方法（DataStore 层）

新增基准程序：`build/tests/bench_data_store_sync`

```bash
# 64KB 对象，500 次写入
./build/tests/bench_data_store_sync 65536 500 /tmp/minis3_sync_bench_run1

# 1MB 对象，120 次写入
./build/tests/bench_data_store_sync 1048576 120 /tmp/minis3_sync_bench_run2

# 256KB 对象，300 次写入
./build/tests/bench_data_store_sync 262144 300 /tmp/minis3_sync_bench_run3
```

### 实测摘要（同机，2026-03-07）

- 64KB × 500：
  - `fsync`: 1.46 MB/s
  - `fdatasync`: 1.59 MB/s（约 +9.3%）
  - `none`: 360.99 MB/s（约 248x）
- 256KB × 300：
  - `fsync`: 6.63 MB/s
  - `fdatasync`: 6.47 MB/s（约 -2.5%）
  - `none`: 423.89 MB/s（约 63.9x）
- 1MB × 120：
  - `fsync`: 23.83 MB/s
  - `fdatasync`: 20.10 MB/s（约 -15.6%）
  - `none`: 465.23 MB/s（约 19.5x）

### 结论与建议

1. 当前机器与负载下，`fdatasync` 并不稳定优于 `fsync`，需按真实业务负载复测后再决定。
2. `none` 吞吐提升显著，但仅建议用于可容忍数据丢失的场景（如压测、临时缓存、离线导入中间态）。
3. 生产环境建议默认 `fsync`；若追求吞吐并可接受有限风险，可在充分演练后评估 `fdatasync`。

## 2026-03-07 第二轮优化：元数据热路径与死锁重试

### 优化点

1. `MetaStore` 增加 bucket 元数据进程内缓存（按 bucket 名）
  - 减少对象 API 路径中重复 `GetBucket` 的 SQL 查询。
2. 为事务写路径增加有限重试（最多 3 次）
  - 覆盖 `PutObjectWithRefCount`、`PutPartWithRefCount`。
  - 针对 `Deadlock found` 和 `Lock wait timeout exceeded` 做短退避重试。

### 对比测试（同机，同参数）

测试命令（4KB 对象，20 并发，200 次上传）：

```bash
./scripts/bench_upload.sh http://localhost:8080 <bucket> /tmp/minis3_bench_4k.bin 20 200
```

结果（优化前 -> 优化后）：

- Operations: `8.17 ops/s` -> `8.09 ops/s`（约 `-0.98%`）
- Throughput: `~0.03 MB/s` -> `~0.03 MB/s`（基本持平）
- 成功率：`199/200`（有 500）-> `200/200`（全成功）

### 分析

1. **可靠性显著改善**：
  - 事务重试将死锁从“用户可见失败”转为“内部重试后成功”，减少 500。

2. **吞吐提升不明显的原因**：
  - 当前压测使用固定 4KB 内容，所有请求命中同一 `cas_key`，导致 `cas_blobs` 热点锁竞争。
  - 该场景下核心瓶颈是 MySQL 行级锁竞争，不是 bucket 查询本身，因此缓存收益被掩盖。

3. **下一步建议（第三轮）**：
  - 优化 `cas_blobs` 更新策略以降低热点竞争（例如分桶计数或异步聚合）。
  - 增加“随机内容上传”压测口径，评估 bucket 缓存对常规业务流量的真实收益。

## 2026-03-07 第三轮优化：新对象写入快速路径

### 优化点

在 `MetaStore::PutObjectWithRefCount` 引入“新对象 key 快速事务路径”：

1. 先执行快速路径事务：
  - `cas_blobs` 引用计数 `+1`
  - `objects` 直接 `INSERT`（非 `UPSERT`）
2. 若对象键重复（`Duplicate entry`），回退到原有覆盖慢路径（`SELECT ... FOR UPDATE` + 引用计数调整）。
3. 保留第二轮中的事务重试（死锁/锁等待超时）。

目的：

- 对“新对象写入（主流上传）”减少锁范围与 SQL 往返；
- 将复杂锁逻辑仅留给覆盖写场景。

### 测试结果

#### A) 同口径主对比（固定 4KB 内容，20 并发，200 次）

命令：

```bash
./scripts/bench_upload.sh http://localhost:8080 <bucket> /tmp/minis3_bench_4k.bin 20 200
```

结果：

- 第二轮后：`8.09 ops/s`，成功 `200/200`
- 第三轮后：`9.48 ops/s`，成功 `200/200`

提升：

- QPS 约 `+17.2%`
- 吞吐（MB/s）在小对象场景下同向提升（脚本显示四舍五入后仍约 `0.03`）

#### B) 补充口径（随机 4KB 内容，20 并发，200 次）

结果：

- `12.63 ops/s`，成功 `200/200`，吞吐约 `0.04 MB/s`

### 分析

1. 第三轮优化在“新 key 上传”场景生效明显，说明快速路径成功减少了慢路径锁开销。
2. 随机内容优于固定内容，表明固定内容下单一 `cas_key` 的热点竞争仍然存在。
3. 当前系统在可靠性上已显著改善（本轮压测无 500），吞吐提升属于结构性优化后的稳步增益。

### 后续建议（第四轮）

1. 针对 `cas_blobs` 热点行进一步降竞争（如分片计数或异步聚合）。
2. 在压测脚本中增加“每请求随机内容”模式，作为默认性能口径之一。
3. 增加服务端分阶段耗时指标（DB/存储/网络）以定位下一阶段瓶颈。

## 2026-03-07 第四轮优化：数据库并发锁策略微调

### 优化内容

1. 在 MySQL 连接建立后设置会话隔离级别为 `READ COMMITTED`（降低写并发下锁冲突概率）。
2. 保持第三轮“新对象快速路径 + 覆盖慢路径回退”结构，避免扩大发布风险。
3. 保留第二轮事务重试策略（死锁/锁等待超时重试）。

### 测试结果

#### A) 同口径主对比（固定 4KB，20 并发，200 次）

- 第三轮：`9.48 ops/s`，成功 `200/200`
- 第四轮：`9.22 ops/s`，成功 `200/200`

变化：约 `-2.7%`（基本持平，轻微回落）

#### B) 随机内容口径（4KB，20 并发，200 次）

- 第三轮：`12.63 ops/s`，成功 `200/200`
- 第四轮：`9.74 ops/s`，成功 `200/200`

变化：约 `-22.9%`

### 分析结论

1. 本轮优化对吞吐提升不明显，主要收益是并发锁冲突风险控制，而非直接提速。
2. 当前瓶颈仍集中在 `cas_blobs` 热点行的高频 `ref_count` 更新，隔离级别调整无法根治该热点。
3. 相比第二轮（`8.09 ops/s`），第四轮总体仍更优；但相对第三轮，未带来额外性能红利。

### 建议（下一轮）

1. 设计 `cas_blobs` 热点分散方案（如分片计数或增量聚合后批量回写）。
2. 增加端到端指标拆分（数据库等待时间、事务重试次数、热点 key 分布），避免仅看总 QPS。
3. 压测固定“冷机/负载背景”条件，降低跨轮结果波动。

## 2026-03-07 多轮优化最终总结（追加）

本节对本次多轮优化进行统一复盘：**方案对比 -> 数据对比 -> 原因分析 -> 当前瓶颈 -> 后续优先级**。

---

### 一、优化方案全景对比

#### 第 1 轮：存储落盘策略可配置（DataStore）

**改动点**

- 新增 `storage.sync_mode`：`fsync | fdatasync | none`
- 将写入落盘行为从固定策略改为配置驱动

**目标**

- 量化“持久性保证 vs 吞吐”的工程权衡边界

**结论**

- `none` 吞吐提升极大，但可靠性风险高（崩溃可丢最近写入）
- `fdatasync` 相比 `fsync` 在本机波动较大，不稳定占优

---

#### 第 2 轮：元数据热路径与死锁可见错误治理（MetaStore）

**改动点**

- bucket 元数据缓存（减少重复 `GetBucket` SQL）
- 事务写路径增加死锁/锁超时重试（`PutObjectWithRefCount`、`PutPartWithRefCount`）

**目标**

- 提升成功率，消除用户可见 `500`

**结论**

- 吞吐基本持平，但稳定性显著提升（`199/200` -> `200/200`）

---

#### 第 3 轮：新对象写入快速路径（MetaStore）

**改动点**

- `PutObjectWithRefCount` 引入“新 key 快速事务路径”
- 新建对象直接 `INSERT`，冲突时回退慢路径（`FOR UPDATE`）

**目标**

- 减少新对象上传时锁开销与 SQL 往返

**结论**

- 同口径固定内容压测显著提升（约 `+17.2%`）
- 随机内容口径提升更明显，说明热点冲突压力下降

---

#### 第 4 轮：并发锁策略微调（MySQL session）

**改动点**

- 会话隔离级别调为 `READ COMMITTED`
- 保持第三轮快速路径与第二轮重试机制

**目标**

- 继续缓解并发锁冲突并提升吞吐

**结论**

- 最终数据相对第三轮无进一步收益，固定口径小幅回落
- 说明“隔离级别微调”不是当前瓶颈的主要解法

---

### 二、关键结果对比（统一口径）

> 统一口径：`4KB`、并发 `20`、`200` 次上传，`bench_upload.sh`

| 阶段 | 主要方案 | Ops/s | 成功率 | 相对上一阶段 |
|---|---|---:|---:|---:|
| 第2轮前基线 | 原始实现 | 8.17 | 199/200 | - |
| 第2轮后 | 缓存 + 重试 | 8.09 | 200/200 | -0.98%（吞吐）+ 成功率提升 |
| 第3轮后 | 新对象快速路径 | 9.48 | 200/200 | +17.2% |
| 第4轮后（最终） | READ COMMITTED + 保持前述优化 | 9.22 | 200/200 | -2.7% |

随机内容补充口径（4KB，20 并发，200 次）：

- 第3轮：`12.63 ops/s`
- 第4轮：`9.74 ops/s`

说明：随机口径在第3轮表现更好，表明第3轮对“新 key 快速上传”更契合。

---

### 三、为什么会产生这些结果（原因拆解）

#### 1) 第2轮“成功率提升明显、吞吐提升有限”

- 重试机制把部分死锁从“失败”变成“延迟后成功”，先改善了可靠性指标。
- 但固定 4KB 内容会反复命中同一 `cas_key`，`cas_blobs.ref_count` 行成为热点，吞吐上限仍被行锁竞争限制。

#### 2) 第3轮“吞吐明显提升”

- 通过“新对象快速路径”，减少了覆盖写慢路径触发概率，缩短了关键事务链路。
- 在对象 key 不重复或重复率低场景，收益最明显。

#### 3) 第4轮“收益不及第3轮”

- `READ COMMITTED` 对某些锁冲突有帮助，但对单行高频 `UPDATE ref_count` 这种热点写竞争作用有限。
- 当前主要瓶颈已不是隔离级别本身，而是**单热点键更新模式**。

---

### 四、当前性能瓶颈（结论）

按影响优先级排序：

1. **`cas_blobs` 热点行写竞争（核心瓶颈）**
  - 固定内容压测中，大量请求竞争同一 `cas_key` 的 `ref_count` 更新。

2. **元数据事务链路仍偏重**
  - 即使有快速路径，覆盖写与部分冲突场景仍需多 SQL + 锁。

3. **压测脚本口径对热点放大明显**
  - 固定内容使去重命中率极高，导致与真实业务分布存在偏差。

4. **观察维度不足**
  - 缺少“事务重试次数、锁等待时长、热点 cas_key 频率”指标，定位仍偏粗粒度。

---

### 五、从结果反推：最值得继续做的优化

#### P0（最优先）

1. **`cas_blobs` 热点分散**
  - 方案方向：分片计数、增量累积后批量回写、异步聚合。
  - 预期：直接缓解单行写争用，是最可能带来下一次显著增益的点。

2. **增加数据库锁与重试指标**
  - 新增 metrics：事务重试次数、死锁次数、锁等待时间、热点 key topN。

#### P1（高价值）

3. **压测口径标准化**
  - 固定内容 + 随机内容双口径并行保留；
  - 固定并发、固定 CPU 负载背景、固定磁盘状态。

4. **覆盖写路径继续瘦身**
  - 压缩慢路径 SQL 往返，减少不必要的读后写。

---

### 六、最终结论（本轮阶段性）

1. 本次多轮优化已把系统从“有失败的低吞吐”推进到“稳定全成功 + 中等幅度提速”。
2. **最有效的一轮是第3轮**（新对象快速路径）。
3. **当前主要短板已收敛为 `cas_blobs` 热点写竞争**，后续收益需要围绕该点做结构性改造。
4. 建议后续优化目标从“参数微调”切换到“热点写路径重构”。

---

## 面试答辩版（可直接讲）

### A. 3分钟口述稿

面试官好，我这个项目是一个单机对象存储系统，核心链路是 HTTP 接入、流式上传、MySQL 元数据事务、CAS 文件存储和后台 GC。我的优化目标是提升小对象 PUT 的吞吐和稳定性。

第一轮我先把存储落盘策略做成可配置，支持 `fsync/fdatasync/none`，并做了专项基准。结论是：`none` 吞吐极高但可靠性风险大；`fdatasync` 在我机器上不稳定优于 `fsync`，所以生产默认仍保守选 `fsync`。

第二轮我优先做稳定性治理：增加 bucket 元数据缓存，给事务写路径加死锁和锁等待超时重试。结果是吞吐基本持平，但成功率从 `199/200` 提升到 `200/200`，说明用户可见 500 明显下降。

第三轮是最有效的一轮：在对象写入路径加入“新对象快速路径”，新 key 直接走轻量事务，只有冲突时才回退慢路径。固定口径下 QPS 从 `8.09` 提升到 `9.48`，约 `+17.2%`；随机内容口径到 `12.63`，说明热点冲突被部分缓解。

第四轮我尝试把数据库会话隔离级别调整为 `READ COMMITTED`。结果相对第三轮没有继续提升，固定口径约 `9.22`。这说明当前主要矛盾不是隔离级别，而是 `cas_blobs.ref_count` 的热点写竞争。

最终我得到的结论是：先保稳定、再提吞吐、最后定位瓶颈。这几轮优化形成了完整工程闭环。下一步最值得做的是热点分散方案，比如分片计数或异步聚合回写，而不是继续做参数微调。

---

### B. 1页答辩表格

| 轮次 | 主要问题 | 优化方案 | 结果（核心数据） | 结果原因 | 结论 |
|---|---|---|---|---|---|
| 第1轮 | 写入性能与可靠性权衡不清 | 落盘策略可配置（`fsync/fdatasync/none`） | `none` 吞吐最高；`fdatasync` 对 `fsync` 优势不稳定 | I/O flush 语义与设备特性导致波动 | 生产默认 `fsync`，压测可用 `none` |
| 第2轮 | 并发下出现 500（死锁可见） | bucket 缓存 + 死锁/锁超时重试 | `8.17 -> 8.09 ops/s`（持平）；`199/200 -> 200/200` | 重试吸收失败，但热点锁未消失 | 稳定性显著提升 |
| 第3轮 | PUT 慢路径锁开销高 | 新对象快速路径，冲突回退慢路径 | `8.09 -> 9.48 ops/s`（`+17.2%`）；随机口径 `12.63 ops/s` | 新 key 场景减少锁与 SQL 往返 | 最有效一轮 |
| 第4轮 | 希望继续缓解锁冲突 | `READ COMMITTED` 会话隔离级别微调 | 固定口径 `9.22 ops/s`，随机口径 `9.74 ops/s` | 参数微调无法根治热点单行更新 | 相对第3轮无新增红利 |
| 当前瓶颈 | 高并发热点写争用 | `cas_blobs.ref_count` 单行高频更新 | 吞吐上限受行锁竞争限制 | 固定内容压测放大同一 `cas_key` 热点 | 需做结构性改造 |

---

### C. 面试官追问10题 + 高分回答模板

> 使用方式：先给“结论一句话”，再补“证据数据”，最后说“下一步方案”。

#### Q1：你为什么先做稳定性优化，而不是直接追求 QPS？

**高分回答模板**

我先做稳定性是因为线上价值排序里“正确返回”优先于“更快返回”。第二轮里我把成功率从 `199/200` 提升到 `200/200`，虽然吞吐变化不大，但先消除了用户可见 500。这样后续做吞吐优化时，数据更可信，避免把错误率波动误当成性能提升。

---

#### Q2：第3轮为什么能提升 `+17.2%`？核心机制是什么？

**高分回答模板**

核心是把高频新对象写入从“慢路径锁流程”中剥离出来。新 key 直接走快速事务，只有冲突才回退 `FOR UPDATE` 的慢路径。这样减少了锁持有与 SQL 往返，所以固定口径从 `8.09` 提到 `9.48 ops/s`。这属于路径级优化，不是参数调优。

---

#### Q3：为什么第4轮隔离级别调整没有继续提升？

**高分回答模板**

因为主要瓶颈不是事务隔离级别，而是 `cas_blobs.ref_count` 的热点单行更新。`READ COMMITTED` 可以减少一部分锁冲突，但无法消除同一行高频写争用，所以结果相对第3轮基本持平甚至回落。这说明我已经从“猜测瓶颈”转到“定位瓶颈”。

---

#### Q4：你如何证明当前瓶颈是热点写竞争而不是网络或磁盘？

**高分回答模板**

我通过口径对照来证明：固定内容压测会命中同一 `cas_key`，QPS更低；随机内容口径明显更高。这个差异指向元数据热点行争用，而不是网络和磁盘瓶颈。如果是网络/磁盘瓶颈，固定与随机内容不应有这么显著分叉。

---

#### Q5：你做了哪些“可回滚、低风险”的工程控制？

**高分回答模板**

我尽量把高风险改动做成可配置或可回退：第一轮把落盘策略放到配置层；后续优化保持慢路径兜底；引入重试时设置上限和退避，避免无限重试。每轮都先构建、再单测、再集成测、再压测，保证变更可控。

---

#### Q6：如果要上线，你会怎么灰度第3轮优化？

**高分回答模板**

我会按流量分层灰度：先低流量 bucket，再扩大到普通业务 bucket。监控重点看 4 项：成功率、P99、死锁重试次数、热点 key 分布。如果其中任一指标异常，就回退到慢路径或关闭快速路径开关。

---

#### Q7：重试会不会放大系统负载？你怎么控制？

**高分回答模板**

会，所以我做了三个限制：最大重试次数、线性退避、仅对可重试错误（死锁/锁等待超时）生效。这样重试只在“短暂冲突”时起作用，避免对不可恢复错误重复施压。

---

#### Q8：你的优化指标体系是什么？为什么这样选？

**高分回答模板**

我用“成功率 + Ops/s + 口径一致性”三层指标。成功率用于保障业务可用，Ops/s衡量吞吐，口径一致性保证对比公平（同对象大小、同并发、同迭代次数）。这是性能工程里最小可用的可信评估框架。

---

#### Q9：如果给你一周时间继续优化，你的计划是什么？

**高分回答模板**

我会优先做 `cas_blobs` 热点分散（分片计数或异步聚合回写），这是最大杠杆。并补充数据库侧指标：锁等待时间、重试次数、热点 key TopN。目标不是“盲目提速”，而是“先证明瓶颈再对点改造”。

---

#### Q10：这几轮优化你最大的工程收获是什么？

**高分回答模板**

最大的收获是形成了完整闭环：先定义口径，再做分层优化，再用数据验证，再根据结果修正方向。最终确认第3轮最有效、第4轮收益有限，避免了在错误方向持续投入。这是我认为最关键的性能工程能力。

---

### D. 快速收尾话术（30秒）

这次优化不是单次“提速技巧”，而是基于数据的多轮迭代：先把错误率打下来，再做路径级提速，最后识别结构性瓶颈。当前最值得投入的是 `cas_blobs` 热点写竞争治理；如果继续推进，我会优先做分片计数/异步聚合，并配套更细粒度观测指标来闭环验证。

---

## 2026-03-07 第五轮修复：一致性与并发安全缺陷收敛（设计缺陷修复）

本轮目标：修复已发现的 4 类高风险设计缺陷，重点是“数据文件与元数据跨系统一致性”与“删除/读取并发语义”。

### 1) 缺陷一：PUT 成功写入文件，但 MySQL 元数据失败 -> 孤儿 CAS 文件

**问题现象**

- 上传路径先写 `DataStore`，再写 `MetaStore`；当元数据写失败时，磁盘上会残留无引用文件。

**修复策略**

- 在对象 PUT 与分片 PUT 路径加入“失败补偿清理”：
  - 上传前预测 `cas_key`（`SHA256(body)`）并判断是否原先已存在。
  - 若本次确实新写入且后续元数据写失败，则执行“仅清理无引用 CAS”逻辑。
  - 清理逻辑先查 `cas_blobs`：
   - 若不存在元数据记录（`NoSuchKey`），直接删除文件；
   - 若存在且 `ref_count == 0`，删除文件；
   - 否则跳过，避免误删。

**落地位置**

- `src/api/handlers/object_handlers.cpp`
- `src/api/handlers/multipart_handlers.cpp`

---

### 2) 缺陷二：DELETE 与 GET 并发时，GET 可能在发送阶段开文件失败并直接断连

**问题现象**

- GET 先读取元数据并构造文件响应，真正 `open` 发生在异步发送阶段。
- 若期间文件被删除，连接层原行为是直接 `HandleClose()`，客户端拿到连接中断而非明确 HTTP 错误。

**修复策略**

- 当文件发送阶段 `open` 失败时，不再直接断连：
  - 清空待发缓冲，改为返回标准错误响应；
  - `ENOENT` 返回 `404 Object data not found`；
  - 其他错误返回 `500 Failed to open object data`。

**落地位置**

- `src/net/http/http_connection.cpp`

---

### 3) 缺陷三：Range 下载发送长度与响应头长度不一致风险

**问题现象**

- 文件响应中 `Content-Length` 已按 Range 计算，但发送端剩余字节使用了整文件长度字段，可能造成协议不一致。

**修复策略**

- 发送阶段剩余长度改为 `resp.FileLength()`，与响应头长度一致。

**落地位置**

- `src/net/http/http_connection.cpp`

---

### 4) 缺陷四：GC 可能误删“被重新引用”的 CAS（竞态窗口）

**问题现象**

- 旧逻辑是队列里拿到 key 后直接删物理文件，随后删 `cas_blobs`。
- 若 key 在队列等待期间或删除过程中被重新引用，可能出现“元数据有效但文件已删”的严重不一致。

**修复策略（两层防护）**

1. **元数据可回收判定 API**：新增 `MetaStore::CanDeleteCasBlob`，在事务中校验：
  - `cas_blobs.ref_count == 0`
  - `objects` 无引用
  - `multipart_parts` 无引用

2. **GC 隔离删除 + 二次校验**：
  - 删除前先做一次可回收检查；
  - 把 CAS 文件先 `rename` 到 quarantine 临时路径（同盘原子）；
  - 再做一次可回收检查；
  - 若检查失败则把文件恢复回原路径；
  - 仅在二次检查通过时真正删除临时文件。

3. **回调删元数据前再确认**：
  - GC 回调不再无条件 `DeleteCasBlob`，先 `CanDeleteCasBlob` 再删。

**落地位置**

- `src/db/meta_store.h`
- `src/db/meta_store.cpp`
- `src/storage/gc.cpp`
- `src/server/server.cpp`

---

### 验证结果（本轮代码级修复后）

已执行构建与测试：

1. **全量构建（CMake）**：成功
2. **单元测试（unit_tests）**：通过
3. **集成测试（integration_tests）**：通过

说明：本轮主要是正确性修复（consistency/safety），不是吞吐优化；性能数值不应直接与前几轮“提速”做同类对比。

---

### 设计层面的剩余风险与后续建议

虽然本轮已显著缩小不一致窗口，但分布式/高并发系统中跨 DB + 文件系统的一致性无法靠单点改动“绝对消除”。建议后续继续做：

1. 增加后台对账任务（扫描 `objects/multipart_parts` 与 CAS 文件存在性差异）；
2. 对 “data exists but metadata missing” 与 “metadata exists but data missing” 分别建立自动修复策略；
3. 增加一致性指标：
  - orphan file 数量
  - missing data file 数量
  - GC quarantine 恢复次数
  - CAS 可回收检查失败次数

这会把当前“修复逻辑”升级为“可观测、可审计、可自愈”的一致性体系。

---

## 2026-03-07 第六轮修复：代码审查新增缺陷修复与验证

本轮是在第五轮基础上继续做“细粒度代码审查”，修复了 3 个容易被忽略但会影响稳定性/协议正确性的 bug。

### 1) `ByteBuffer::Shrink` 写指针计算错误

**问题**

- `Shrink()` 中 `writer_index_` 使用了更新后的 `reader_index_` 与旧状态混算，可能导致写指针错误，进而影响后续 buffer 行为。

**修复**

- 先缓存 `readable`，再基于该固定值重建 `writer_index_`。

**落地文件**

- `src/net/buffer/byte_buffer.cpp`

---

### 2) HTTP 头部解析对非法 `Content-Length` 与冲突头校验不足

**问题**

- 非法 `Content-Length`（例如包含非数字尾缀）会被当作 0 继续处理；
- `Content-Length` 与 `Transfer-Encoding: chunked` 同时出现时未显式拒绝，存在协议歧义风险；
- `Transfer-Encoding` 值匹配 `chunked` 时大小写不敏感处理不足。

**修复**

- `FinishHeaders` 改为返回 `bool`，在 header 收尾阶段做严格校验；
- `Content-Length` 使用 `from_chars` 严格全量消费校验，非法立即报错；
- 检测到 `Content-Length` 与 `chunked` 并存时直接报错；
- `Transfer-Encoding` 值转小写后匹配 `chunked`。

**落地文件**

- `src/net/http/http_parser.h`
- `src/net/http/http_parser.cpp`

---

### 3) 零字节对象的 Range 解析下溢

**问题**

- `ParseRange` 在 `file_size == 0` 时会走 `file_size - 1` 逻辑，触发 `size_t` 下溢。

**修复**

- `ParseRange` 开头增加 `file_size == 0` 直接拒绝（返回不可满足范围）。

**落地文件**

- `src/api/handlers/object_handlers.cpp`

---

### 验证结果

本轮修改后执行：

1. `Build_CMakeTools`：构建成功；
2. `RunCtest_CMakeTools`：
  - `unit_tests` 通过；
  - `integration_tests` 通过。

结论：本轮新增修复已通过现有回归测试，未引入可见构建或测试回归。
