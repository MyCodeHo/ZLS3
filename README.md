# MiniS3-CPP

[![C++](https://img.shields.io/badge/C++-20-blue.svg)](https://isocpp.org/)
[![Linux](https://img.shields.io/badge/Platform-Linux-green.svg)](https://www.linux.org/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

一个高性能的单机对象存储服务，使用 C++20 实现，提供核心对象存储 REST API。

## ✨ 核心特性

- ✅ **高性能** - 基于 epoll 的异步 I/O，支持高并发
- ✅ **流式上传/下载** - 大文件不进内存，支持 TB 级文件
- ✅ **Range 断点下载** - 支持 `Range: bytes=start-end`
- ✅ **Multipart 分片上传** - 支持断点续传
- ✅ **CAS 内容寻址** - 基于 SHA256 自动去重（秒传）
- ✅ **幂等性保证** - Idempotency-Key 支持
- ✅ **预签名 URL** - 临时下载/上传链接
- ✅ **可观测性** - 结构化日志
- ✅ **垃圾回收** - 自动清理无引用的 CAS 文件

## 🚀 快速开始

### 使用 Docker（推荐）

```bash
# 克隆项目
git clone https://github.com/yourname/minis3-cpp.git
cd minis3-cpp

# 启动服务（Docker Compose v2 或 v1）
docker compose -f docker/docker-compose.yaml up -d
# 或
docker-compose -f docker/docker-compose.yaml up -d

# 测试 API
curl http://localhost:8080/healthz
```

### 手动编译

```bash
# 安装依赖 (Ubuntu/Debian)
apt-get install -y build-essential cmake libmysqlclient-dev libssl-dev \
    libyaml-cpp-dev libspdlog-dev nlohmann-json3-dev uuid-dev

# 编译
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# 运行
./minis3_server ../configs/server.example.yaml
```

## 📖 API 示例

```bash
# 创建 Bucket
curl -X PUT http://localhost:8080/buckets/my-bucket \
    -H "Authorization: Bearer test-token"

# 上传对象
curl -X PUT http://localhost:8080/buckets/my-bucket/objects/hello.txt \
    -H "Authorization: Bearer test-token" \
    -H "Content-Type: text/plain" \
    -d "Hello, World!"

# 下载对象
curl http://localhost:8080/buckets/my-bucket/objects/hello.txt \
    -H "Authorization: Bearer test-token"

# Range 下载
curl http://localhost:8080/buckets/my-bucket/objects/hello.txt \
    -H "Authorization: Bearer test-token" \
    -H "Range: bytes=0-4"

# 删除对象
curl -X DELETE http://localhost:8080/buckets/my-bucket/objects/hello.txt \
    -H "Authorization: Bearer test-token"
```

更多 API 文档请查看 [docs/api.md](docs/api.md)

## 🏗️ 架构设计

```
┌─────────────────────────────────────────────────────────────┐
│                        Client                                │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    HTTP/REST API                             │
│              (PUT/GET/HEAD/DELETE/POST)                      │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                     Net Layer                                │
│        ┌──────────────────────────────────────┐             │
│        │  Acceptor (1 thread)                  │             │
│        │       │                               │             │
│        │       ▼ round-robin                   │             │
│        │  ┌─────────┬─────────┬─────────┐     │             │
│        │  │ IO Loop │ IO Loop │ IO Loop │     │             │
│        │  │  (N)    │   (N)   │   (N)   │     │             │
│        │  └─────────┴─────────┴─────────┘     │             │
│        └──────────────────────────────────────┘             │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   Service Layer                              │
│    ┌──────────┬──────────┬──────────┬──────────┐           │
│    │ Object   │ Bucket   │Multipart │ Presign  │           │
│    │ Service  │ Service  │ Service  │ Service  │           │
│    └──────────┴──────────┴──────────┴──────────┘           │
└─────────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┴───────────────┐
              ▼                               ▼
┌─────────────────────────┐     ┌─────────────────────────┐
│      DataStore          │     │       MetaStore         │
│   (CAS File System)     │     │    (MySQL/InnoDB)       │
│                         │     │                         │
│  /data/cas/aa/bb/xxx    │     │  buckets, objects       │
│  /tmp/upload/xxx        │     │  cas_blobs, multipart   │
└─────────────────────────┘     └─────────────────────────┘
```

## 🚀 快速开始

### 依赖安装 (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake \
    libssl-dev \
    libmysqlclient-dev \
    libyaml-cpp-dev \
    libspdlog-dev \
    nlohmann-json3-dev \
    uuid-dev
```

### 编译

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 启动 MySQL（使用 Docker）

```bash
## 📖 Multipart 示例

```bash
# 1. 初始化
UPLOAD_ID=$(curl -X POST \
  -H "Authorization: Bearer dev-token-12345" \
  http://localhost:8080/buckets/default/multipart/bigfile.zip | jq -r .upload_id)

# 2. 上传分片
curl -X PUT \
  -H "Authorization: Bearer dev-token-12345" \
  --data-binary @part1.bin \
  http://localhost:8080/buckets/default/multipart/bigfile.zip/$UPLOAD_ID/parts/1

# 3. 完成上传
curl -X POST \
  -H "Authorization: Bearer dev-token-12345" \
  -H "Content-Type: application/json" \
  -d '{"parts":[{"part_number":1,"etag":"..."}]}' \
  http://localhost:8080/buckets/default/multipart/bigfile.zip/$UPLOAD_ID/complete
- **日志**: spdlog
- **配置**: yaml-cpp
- **JSON**: nlohmann/json

## 📁 项目结构

```
MiniS3-CPP/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── design.md          # 设计文档
│   ├── api.md             # API 文档
│   └── perf.md            # 性能报告
├── configs/
│   └── server.example.yaml
├── cmd/
│   └── minis3_server/     # 入口
├── src/
│   ├── net/               # 网络层
│   ├── util/              # 工具类
│   ├── db/                # 数据库层
│   ├── storage/           # 存储层
│   ├── service/           # 业务层
│   └── api/               # API 层
├── scripts/
├── docker/
└── tests/
```

## 📄 License

MIT License
