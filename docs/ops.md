# MiniS3 运维指南

## 部署

### Docker 部署（推荐）

```bash
# 使用 Docker Compose v2
docker compose -f docker/docker-compose.yaml up -d

# 或 Docker Compose v1
docker-compose -f docker/docker-compose.yaml up -d

# 查看日志
docker-compose logs -f minis3

# 停止服务
docker-compose down
```

### 手动部署

1. 安装依赖：

```bash
apt-get install -y mysql-client libmysqlclient-dev libssl-dev libyaml-cpp-dev
```

2. 编译：

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

3. 初始化数据库：

```bash
mysql -u root -p < mysql_init.sql
```

4. 配置：

```bash
cp configs/server.example.yaml /etc/minis3/server.yaml
# 编辑配置文件
```

5. 启动：

```bash
./minis3_server /etc/minis3/server.yaml
```

## 配置

### 主配置文件

```yaml
server:
  listen_ip: 0.0.0.0
  listen_port: 8080
  io_threads: 4
  worker_threads: 8

mysql:
  host: localhost
  port: 3306
  user: minis3
  password: your_password
  database: minis3
  pool_size: 20

storage:
  data_dir: /data/minis3/cas
  tmp_dir: /data/minis3/tmp

auth:
  static_tokens:
    - "your-secret-token"

log:
  level: "info"
  access_log_path: "/var/log/minis3/access.log"
  error_log_path: "/var/log/minis3/error.log"
  json_format: true

gc:
  enabled: true
  interval_seconds: 300
  batch_size: 100
```

## 日常运维

### 服务管理

```bash
# 使用 systemd
systemctl start minis3
systemctl stop minis3
systemctl status minis3
systemctl restart minis3

# 查看日志
journalctl -u minis3 -f
```

### 健康检查

```bash
# 基本健康检查
curl http://localhost:8080/healthz

# 就绪检查
curl http://localhost:8080/readyz

# Prometheus 指标
curl http://localhost:8080/metrics
```

### 日志管理

日志格式：JSON

```json
{
  "timestamp": "2024-01-01T00:00:00.000Z",
  "level": "INFO",
  "message": "Request completed",
  "trace_id": "uuid...",
  "method": "GET",
  "path": "/bucket/key",
  "status": 200,
  "duration_ms": 5
}
```

日志轮转配置：

```
/var/log/minis3/*.log {
    daily
    rotate 7
    compress
    delaycompress
    missingok
    notifempty
    create 0640 minis3 minis3
}
```

## 数据库维护

### 定期维护

```sql
-- 优化表
OPTIMIZE TABLE buckets, objects, cas_blobs, multipart_uploads, multipart_parts;

-- 清理过期的幂等性记录
DELETE FROM idempotency_records WHERE expires_at < NOW();

-- 清理过期的分片上传
DELETE FROM multipart_parts WHERE upload_id IN (
  SELECT upload_id FROM multipart_uploads WHERE created_at < DATE_SUB(NOW(), INTERVAL 7 DAY)
);
DELETE FROM multipart_uploads WHERE created_at < DATE_SUB(NOW(), INTERVAL 7 DAY);
```

### 备份策略

```bash
# 全量备份
mysqldump -u root -p minis3 > minis3_backup_$(date +%Y%m%d).sql

# 增量备份（使用 binlog）
mysqlbinlog --start-datetime="2024-01-01 00:00:00" /var/lib/mysql/mysql-bin.* > incremental.sql

# 数据目录备份
tar -czvf cas_backup_$(date +%Y%m%d).tar.gz /data/minis3/cas
```

### 恢复

```bash
# 恢复数据库
mysql -u root -p minis3 < minis3_backup.sql

# 恢复数据目录
tar -xzvf cas_backup.tar.gz -C /
```

## 垃圾回收

### GC 工作原理

1. 扫描 `cas_blobs` 表中 `ref_count = 0` 的记录
2. 检查记录年龄是否超过 `min_age_seconds`
3. 删除对应的 CAS 文件
4. 删除数据库记录

### 手动触发 GC

```bash
# 通过 API
curl -X POST http://localhost:8080/admin/gc -H "Authorization: Bearer admin-token"

# 或发送 SIGUSR1 信号
kill -USR1 $(pgrep minis3_server)
```

### GC 配置

```yaml
gc:
  enabled: true
  interval_seconds: 3600    # 每小时运行一次
  min_age_seconds: 86400    # 保留 24 小时
  batch_size: 100           # 每批处理数量
```

## 监控告警

### 告警规则

```yaml
groups:
- name: minis3
  rules:
  - alert: HighErrorRate
    expr: rate(minis3_http_requests_total{status=~"5.."}[5m]) / rate(minis3_http_requests_total[5m]) > 0.01
    for: 5m
    labels:
      severity: critical
    annotations:
      summary: "High error rate detected"

  - alert: HighLatency
    expr: histogram_quantile(0.99, rate(minis3_http_request_duration_seconds_bucket[5m])) > 0.5
    for: 5m
    labels:
      severity: warning
    annotations:
      summary: "High P99 latency"

  - alert: LowDiskSpace
    expr: node_filesystem_avail_bytes{mountpoint="/data"} / node_filesystem_size_bytes{mountpoint="/data"} < 0.1
    for: 5m
    labels:
      severity: critical
    annotations:
      summary: "Low disk space"
```

## 故障排查

### 常见问题

#### 1. 连接被拒绝

```bash
# 检查服务状态
systemctl status minis3

# 检查端口
ss -tlnp | grep 8080

# 检查防火墙
iptables -L -n
```

#### 2. 数据库连接失败

```bash
# 测试连接
mysql -h localhost -u minis3 -p -e "SELECT 1"

# 检查连接数
mysql -e "SHOW PROCESSLIST"
mysql -e "SHOW GLOBAL STATUS LIKE 'Threads_connected'"
```

#### 3. 磁盘空间不足

```bash
# 检查磁盘使用
df -h

# 查找大文件
find /data/minis3 -type f -size +100M

# 手动触发 GC
curl -X POST http://localhost:8080/admin/gc
```

### 调试模式

```bash
# 启用调试日志
export MINIS3_LOG_LEVEL=debug
./minis3_server -c config.yaml

# 或修改配置
logging:
  level: debug
```
