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
