#include "object_service.h"
#include "util/crypto.h"
#include "util/logging.h"

namespace minis3 {

ObjectService::ObjectService(MetaStore& meta_store, DataStore& data_store)
    : meta_store_(meta_store), data_store_(data_store) {
}

Result<std::string> ObjectService::PutObject(int64_t bucket_id, const std::string& key,
                                              const std::string& content, const std::string& content_type,
                                              const std::string& owner_id) {
    // 计算内容的 SHA256
    std::string sha256 = Crypto::SHA256(content);
    int64_t size = static_cast<int64_t>(content.size());
    
    // 检查 CAS 是否存在
    bool cas_exists = data_store_.Exists(sha256);
    
    if (!cas_exists) {
        // 写入 CAS 文件
        auto write_result = data_store_.Write(content);
        if (!write_result.ok()) {
            return Result<std::string>::Err(write_result.status());
        }
    }
    
    // 注册 CAS Blob
    auto cas_result = meta_store_.RegisterCasBlob(sha256, size);
    if (!cas_result.ok()) {
        return Result<std::string>::Err(cas_result.status());
    }
    
    // 检查是否已存在相同对象
    auto exists_result = ObjectExists(bucket_id, key);
    if (!exists_result.ok()) {
        return Result<std::string>::Err(exists_result.status());
    }
    
    if (exists_result.value()) {
        // 获取旧对象信息
        auto old_info_result = HeadObject(bucket_id, key);
        if (old_info_result.ok() && old_info_result.value().sha256_hash != sha256) {
            // 减少旧 CAS 的引用计数
            meta_store_.DecrementRefCount(old_info_result.value().sha256_hash);
        }
    }
    
    // 创建或更新对象
    std::string etag = "\"" + sha256 + "\"";
    auto put_result = meta_store_.PutObject(bucket_id, key, size, etag, content_type, sha256, owner_id);
    if (!put_result.ok()) {
        return Result<std::string>::Err(put_result.status());
    }
    
    return Result<std::string>::Ok(sha256);
}

Result<std::pair<std::string, ObjectInfo>> ObjectService::GetObject(int64_t bucket_id, const std::string& key) {
    // 获取对象元信息
    auto info_result = HeadObject(bucket_id, key);
    if (!info_result.ok()) {
        return Result<std::pair<std::string, ObjectInfo>>::Err(info_result.status());
    }
    
    const auto& info = info_result.value();
    
    // 读取 CAS 内容
    auto content_result = data_store_.Read(info.sha256_hash);
    if (!content_result.ok()) {
        return Result<std::pair<std::string, ObjectInfo>>::Err(content_result.status());
    }
    
    return Result<std::pair<std::string, ObjectInfo>>::Ok(
        std::make_pair(content_result.value(), info));
}

Result<std::pair<std::string, ObjectInfo>> ObjectService::GetObjectRange(int64_t bucket_id, const std::string& key,
                                                                          int64_t start, int64_t end) {
    // 获取对象元信息
    auto info_result = HeadObject(bucket_id, key);
    if (!info_result.ok()) {
        return Result<std::pair<std::string, ObjectInfo>>::Err(info_result.status());
    }
    
    const auto& info = info_result.value();
    
    // 验证范围
    if (start < 0 || start >= info.size) {
        return Result<std::pair<std::string, ObjectInfo>>::Err(
            Status::InvalidArgument("Invalid range start"));
    }
    
    if (end < 0) {
        end = info.size - 1;
    }
    
    if (end >= info.size) {
        end = info.size - 1;
    }
    
    if (start > end) {
        return Result<std::pair<std::string, ObjectInfo>>::Err(
            Status::InvalidArgument("Invalid range: start > end"));
    }
    
    // 读取范围内容
    std::string content;
    auto status = data_store_.StreamRead(info.sha256_hash, start, end - start + 1,
        [&content](const char* data, size_t len) {
            content.append(data, len);
            return true;
        });
    
    if (!status.ok()) {
        return Result<std::pair<std::string, ObjectInfo>>::Err(status);
    }
    
    return Result<std::pair<std::string, ObjectInfo>>::Ok(
        std::make_pair(content, info));
}

Status ObjectService::DeleteObject(int64_t bucket_id, const std::string& key) {
    // 获取对象信息
    auto info_result = HeadObject(bucket_id, key);
    if (!info_result.ok()) {
        return info_result.status();
    }
    
    const auto& info = info_result.value();
    
    // 删除对象元信息
    auto status = meta_store_.DeleteObject(bucket_id, key);
    if (!status.ok()) {
        return status;
    }
    
    // 减少 CAS 引用计数
    return meta_store_.DecrementRefCount(info.sha256_hash);
}

Result<bool> ObjectService::ObjectExists(int64_t bucket_id, const std::string& key) {
    auto result = meta_store_.GetObject(bucket_id, key);
    if (result.ok()) {
        return Result<bool>::Ok(true);
    }
    if (result.status().IsNotFound()) {
        return Result<bool>::Ok(false);
    }
    return Result<bool>::Err(result.status());
}

Result<ObjectInfo> ObjectService::HeadObject(int64_t bucket_id, const std::string& key) {
    return meta_store_.GetObject(bucket_id, key);
}

Result<ListObjectsResult> ObjectService::ListObjects(int64_t bucket_id, const std::string& prefix,
                                                      const std::string& delimiter,
                                                      const std::string& start_after, int limit) {
    return meta_store_.ListObjects(bucket_id, prefix, delimiter, start_after, limit);
}

Result<std::string> ObjectService::CopyObject(int64_t src_bucket_id, const std::string& src_key,
                                               int64_t dst_bucket_id, const std::string& dst_key,
                                               const std::string& owner_id) {
    // 获取源对象信息
    auto src_info_result = HeadObject(src_bucket_id, src_key);
    if (!src_info_result.ok()) {
        return Result<std::string>::Err(src_info_result.status());
    }
    
    const auto& src_info = src_info_result.value();
    
    // 检查目标是否已存在
    auto dst_exists = ObjectExists(dst_bucket_id, dst_key);
    if (!dst_exists.ok()) {
        return Result<std::string>::Err(dst_exists.status());
    }
    
    if (dst_exists.value()) {
        // 获取旧对象信息
        auto old_info_result = HeadObject(dst_bucket_id, dst_key);
        if (old_info_result.ok() && old_info_result.value().sha256_hash != src_info.sha256_hash) {
            meta_store_.DecrementRefCount(old_info_result.value().sha256_hash);
        }
    }
    
    // 创建或更新对象
    auto put_result = meta_store_.PutObject(dst_bucket_id, dst_key, src_info.size,
                                            src_info.etag, src_info.content_type,
                                            src_info.sha256_hash, owner_id);
    if (!put_result.ok()) {
        return Result<std::string>::Err(put_result.status());
    }
    
    // 增加 CAS 引用计数
    auto inc_status = meta_store_.IncrementRefCount(src_info.sha256_hash);
    if (!inc_status.ok()) {
        return Result<std::string>::Err(inc_status);
    }
    
    return Result<std::string>::Ok(src_info.sha256_hash);
}

} // namespace minis3
