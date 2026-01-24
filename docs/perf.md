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

### 实测结果（2026-01-20，单机本地回环）

测试环境：本机 Linux、MySQL 本地容器、localhost 回环网络，数据目录在本地磁盘。

| 场景 | 并发 | 次数 | 吞吐 | QPS | 延迟 (P99) |
|------|------|------|------|-----|-----------|
| PUT 10MB | 10 | 20 | 36.78 MB/s | 3.67 ops/s | - |
| GET 10MB | 10 | 20 | 7171.97 MB/s | 717.19 ops/s | 0.0076s |
| PUT 100MB | 2 | 5 | 66.43 MB/s | 0.66 ops/s | - |
| GET 100MB | 2 | 5 | 7045.66 MB/s | 70.45 ops/s | 0.0205s |

说明：GET 吞吐量偏高是本机回环与文件缓存导致的结果，不能代表真实磁盘/网络环境。若需更贴近生产，应将数据盘与客户端分离，并在测试前清理缓存。

## 性能优化

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
