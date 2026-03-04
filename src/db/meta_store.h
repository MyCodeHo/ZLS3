#pragma once

#include "mysql_pool.h"
#include "../util/status.h"
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <chrono>

namespace minis3 {

// ===== 数据模型 =====

struct BucketInfo {
    int64_t id;
    std::string name;
    std::string owner_id;
    std::string region;
    std::string acl;
    bool versioning_enabled;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
};

struct ObjectInfo {
    int64_t id;
    int64_t bucket_id;
    std::string key;
    std::string version_id;
    bool is_latest;
    bool is_delete_marker;
    int64_t size;
    std::string etag;
    std::string content_type;
    std::string storage_class;
    std::string sha256_hash;
    std::string metadata_json;
    std::string owner_id;
    std::chrono::system_clock::time_point created_at;
};

struct CasBlobInfo {
    int64_t id;
    std::string sha256_hash;
    int64_t size;
    int32_t ref_count;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_accessed;
};

struct MultipartUploadInfo {
    int64_t id;
    std::string upload_id;
    int64_t bucket_id;
    std::string key;
    std::string content_type;
    std::string metadata_json;
    std::string owner_id;
    std::chrono::system_clock::time_point created_at;
};

struct MultipartPartInfo {
    int64_t id;
    std::string upload_id;
    int32_t part_number;
    std::string sha256_hash;
    int64_t size;
    std::string etag;
    std::chrono::system_clock::time_point created_at;
};

struct IdempotencyRecord {
    std::string idempotency_key;
    std::string request_hash;
    int response_status;
    std::string response_body;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
};

// ===== 列表结果 =====

struct ListObjectsResult {
    std::vector<ObjectInfo> objects;
    std::vector<std::string> common_prefixes;
    bool is_truncated;
    std::string next_continuation_token;
    std::string next_key_marker;
};

struct ListPartsResult {
    std::vector<MultipartPartInfo> parts;
    bool is_truncated;
    int32_t next_part_number_marker;
};

// ===== MetaStore 类 =====

class MetaStore {
public:
    explicit MetaStore(MySQLPool& pool);
    ~MetaStore() = default;
    
    // ===== Bucket 操作 =====
    
    /**
     * 创建 Bucket
     */
    Result<int64_t> CreateBucket(const std::string& name,
                                  const std::string& owner_id,
                                  const std::string& region = "default",
                                  const std::string& acl = "private");
    
    /**
     * 获取 Bucket
     */
    Result<BucketInfo> GetBucket(const std::string& name);
    
    /**
     * 获取 Bucket by ID
     */
    Result<BucketInfo> GetBucketById(int64_t id);
    
    /**
     * 列出所有 Bucket
     */
    Result<std::vector<BucketInfo>> ListBuckets(const std::string& owner_id);
    
    /**
     * 删除 Bucket
     */
    Status DeleteBucket(const std::string& name);
    
    /**
     * 检查 Bucket 是否为空
     */
    Result<bool> IsBucketEmpty(int64_t bucket_id);
    
    // ===== Object 操作 =====
    
    /**
     * 创建/更新 Object
     * @return 新创建的 object_id
     */
    Result<int64_t> PutObject(int64_t bucket_id,
                               const std::string& key,
                               int64_t size,
                               const std::string& etag,
                               const std::string& content_type,
                               const std::string& sha256_hash,
                               const std::string& owner_id,
                               const std::string& metadata_json = "{}");

    /**
     * 创建/更新 Object（带 CAS ref_count 事务）
     * @return 若旧 CAS ref_count 变为 0，返回待 GC 的 cas_key
     */
    Result<std::optional<std::string>> PutObjectWithRefCount(
        int64_t bucket_id,
        const std::string& key,
        int64_t size,
        const std::string& etag,
        const std::string& content_type,
        const std::string& sha256_hash);
    
    /**
     * 获取 Object (最新版本)
     */
    Result<ObjectInfo> GetObject(int64_t bucket_id, const std::string& key);
    
    /**
     * 获取 Object (指定版本)
     */
    Result<ObjectInfo> GetObjectVersion(int64_t bucket_id,
                                        const std::string& key,
                                        const std::string& version_id);
    
    /**
     * 列出 Objects
     */
    Result<ListObjectsResult> ListObjects(int64_t bucket_id,
                                          const std::string& prefix = "",
                                          const std::string& delimiter = "",
                                          const std::string& start_after = "",
                                          int32_t max_keys = 1000);
    
    /**
     * 删除 Object
     */
    Status DeleteObject(int64_t bucket_id, const std::string& key);

    /**
     * 删除 Object（带 CAS ref_count 事务）
     * @return 若 CAS ref_count 变为 0，返回待 GC 的 cas_key
     */
    Result<std::optional<std::string>> DeleteObjectWithRefCount(int64_t bucket_id,
                                                                const std::string& key);
    
    /**
     * 删除 Object (指定版本)
     */
    Status DeleteObjectVersion(int64_t bucket_id,
                               const std::string& key,
                               const std::string& version_id);
    
    // ===== CAS Blob 操作 =====
    
    /**
     * 注册 CAS Blob
     * 如果已存在则增加引用计数
     */
    Result<int64_t> RegisterCasBlob(const std::string& sha256_hash, int64_t size);
    
    /**
     * 获取 CAS Blob
     */
    Result<CasBlobInfo> GetCasBlob(const std::string& sha256_hash);
    
    /**
     * 增加引用计数
     */
    Status IncrementRefCount(const std::string& sha256_hash);
    
    /**
     * 减少引用计数
     */
    Status DecrementRefCount(const std::string& sha256_hash);

    /**
     * 减少引用计数并判断是否为 0
     */
    Result<bool> DecrementRefCountAndCheckZero(const std::string& sha256_hash);
    
    /**
     * 减少 CAS Blob 引用计数（便捷方法）
     */
    Status DecreaseCasBlobRefCount(const std::string& cas_key);
    
    /**
     * 获取可回收的 Blob (引用计数为 0 且超过指定时间)
     */
    Result<std::vector<CasBlobInfo>> GetGarbageBlobs(int32_t min_age_seconds,
                                                     int32_t limit = 100);
    
    /**
     * 删除 CAS Blob 记录
     */
    Status DeleteCasBlob(const std::string& sha256_hash);
    
    // ===== Multipart Upload 操作 =====
    
    /**
     * 创建分片上传
     */
    Result<std::string> CreateMultipartUpload(const std::string& upload_id,
                                              int64_t bucket_id,
                                              const std::string& key,
                                              const std::string& content_type,
                                              const std::string& owner_id,
                                              std::chrono::system_clock::time_point expires_at);
    
    /**
     * 获取分片上传
     */
    Result<MultipartUploadInfo> GetMultipartUpload(const std::string& upload_id);
    
    /**
     * 删除分片上传
     */
    Status DeleteMultipartUpload(const std::string& upload_id);
    
    /**
     * 列出分片上传
     */
    Result<std::vector<MultipartUploadInfo>> ListMultipartUploads(
        int64_t bucket_id,
        const std::string& prefix = "",
        int32_t max_uploads = 1000);
    
    /**
     * 添加/更新分片
     */
    Result<int64_t> CreateOrUpdatePart(const std::string& upload_id,
                                       int32_t part_number,
                                       const std::string& sha256_hash,
                                       int64_t size,
                                       const std::string& etag);

    /**
     * 创建/更新分片（带 CAS ref_count 事务）
     * @return 若旧 CAS ref_count 变为 0，返回待 GC 的 cas_key
     */
    Result<std::optional<std::string>> PutPartWithRefCount(const std::string& upload_id,
                                                           int32_t part_number,
                                                           const std::string& sha256_hash,
                                                           int64_t size,
                                                           const std::string& etag);
    
    /**
     * 添加分片
     */
    Result<int64_t> PutPart(const std::string& upload_id,
                            int32_t part_number,
                            const std::string& sha256_hash,
                            int64_t size,
                            const std::string& etag);
    
    /**
     * 列出分片
     */
    Result<ListPartsResult> ListParts(const std::string& upload_id,
                                      int32_t part_number_marker = 0,
                                      int32_t max_parts = 1000);
    
    /**
     * 完成分片上传
     */
    Status CompleteMultipartUpload(const std::string& upload_id);
    
    /**
     * 取消分片上传
     */
    Status AbortMultipartUpload(const std::string& upload_id);
    
    // ===== 幂等性操作 =====
    
    /**
     * 检查幂等性记录
     */
    Result<std::optional<IdempotencyRecord>> CheckIdempotency(
        const std::string& idempotency_key);
    
    /**
     * 记录幂等性
     */
    Status RecordIdempotency(const std::string& idempotency_key,
                             const std::string& request_hash,
                             int response_status,
                             const std::string& response_body,
                             int32_t ttl_seconds = 86400);
    
    /**
     * 清理过期的幂等性记录
     */
    Status CleanupIdempotencyRecords();

private:
    MySQLPool& pool_;
    
    // 辅助方法
    std::string Escape(const std::string& str);
    std::chrono::system_clock::time_point ParseTimestamp(const char* str);
    std::string GenerateVersionId();
};

} // namespace minis3
