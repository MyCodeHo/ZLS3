# MiniS3 API 文档

## 概述

MiniS3 是一个单机对象存储服务，提供核心 REST API。

## 认证

请求需携带以下任一方式：

```
Authorization: Bearer <token>
```

或：

```
X-API-Key: <key>
```

也可使用预签名 URL（带 `sig` 与 `expires`）。

## API 端点

### 健康检查

#### GET /healthz
检查服务健康状态。

#### GET /readyz
检查依赖是否就绪。

#### GET /metrics
获取 Prometheus 指标。

---

### Bucket 操作

#### PUT /buckets/{bucket}
创建 Bucket。

#### GET /buckets
列出所有 Bucket。

#### GET /buckets/{bucket}
获取 Bucket 元信息。

#### DELETE /buckets/{bucket}
删除空 Bucket。

---

### Object 操作

#### PUT /buckets/{bucket}/objects/{object}
上传对象。

**请求头**
- `Content-Type`
- `Content-Length`
- `Idempotency-Key`（可选）
- `X-Content-SHA256`（可选校验）

**响应**
```json
{
  "bucket": "my-bucket",
  "key": "my-key",
  "etag": "\"sha256...\"",
  "size": 123
}
```

#### GET /buckets/{bucket}/objects/{object}
下载对象（支持 `Range: bytes=start-end`）。

#### HEAD /buckets/{bucket}/objects/{object}
获取对象元数据。

#### DELETE /buckets/{bucket}/objects/{object}
删除对象。

#### GET /buckets/{bucket}/objects
列出对象。

**查询参数**
- `prefix`
- `delimiter`
- `max-keys`
- `start-after`

---

### Multipart

#### POST /buckets/{bucket}/multipart/{object}
初始化分片上传。

#### PUT /buckets/{bucket}/multipart/{object}/{upload_id}/parts/{part_number}
上传分片。

#### POST /buckets/{bucket}/multipart/{object}/{upload_id}/complete
完成上传。

#### DELETE /buckets/{bucket}/multipart/{object}/{upload_id}
中止上传。

---

### 预签名 URL

#### POST /presign
生成预签名 URL。

```json
{
  "bucket": "b",
  "object": "o",
  "method": "GET",
  "ttl_seconds": 600
}
```
{
  "etag": "\"sha256...\""
}
```

#### GET /{bucket}/{key}?uploadId={uploadId}

列出已上传的分片。

**响应**
```json
{
  "bucket": "my-bucket",
  "key": "my-key",
  "upload_id": "uuid...",
  "parts": [
    {
      "part_number": 1,
      "etag": "\"sha256...\"",
      "size": 5242880,
      "last_modified": "2024-01-01T00:00:00Z"
    }
  ]
}
```

#### POST /{bucket}/{key}?uploadId={uploadId}

完成分片上传。

**请求体**
```json
{
  "parts": [
    {"part_number": 1, "etag": "\"sha256...\""},
    {"part_number": 2, "etag": "\"sha256...\""}
  ]
}
```

**响应**
```json
{
  "bucket": "my-bucket",
  "key": "my-key",
  "etag": "\"final-sha256...\""
}
```

#### DELETE /{bucket}/{key}?uploadId={uploadId}

中止分片上传。

**响应**
- `204 No Content` - 中止成功

---

### 预签名 URL

#### GET /presign/get?bucket={bucket}&key={key}&expires={seconds}

生成预签名 GET URL。

**响应**
```json
{
  "url": "http://host/bucket/key?X-Method=GET&X-Expires=...",
  "expires_at": "2024-01-01T01:00:00Z"
}
```

#### GET /presign/put?bucket={bucket}&key={key}&expires={seconds}

生成预签名 PUT URL。

**响应**
```json
{
  "url": "http://host/bucket/key?X-Method=PUT&X-Expires=...",
  "expires_at": "2024-01-01T01:00:00Z"
}
```

---

## 错误响应

所有错误响应的格式：

```json
{
  "error": {
    "code": "NoSuchKey",
    "message": "The specified key does not exist.",
    "trace_id": "uuid..."
  }
}
```

### 错误码

| 错误码 | HTTP 状态码 | 描述 |
|--------|------------|------|
| InvalidArgument | 400 | 请求参数无效 |
| InvalidRange | 400 | Range 请求无效 |
| Unauthorized | 401 | 未认证 |
| AccessDenied | 403 | 访问被拒绝 |
| NoSuchBucket | 404 | Bucket 不存在 |
| NoSuchKey | 404 | 对象不存在 |
| NoSuchUpload | 404 | 分片上传不存在 |
| BucketAlreadyExists | 409 | Bucket 已存在 |
| InternalError | 500 | 内部服务器错误 |
| ServiceUnavailable | 503 | 服务不可用 |
