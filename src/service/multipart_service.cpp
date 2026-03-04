#include "multipart_service.h"
#include "util/crypto.h"
#include "util/uuid.h"
#include "util/logging.h"
#include <chrono>
#include <sstream>
#include <algorithm>

namespace minis3 {

MultipartService::MultipartService(MetaStore& meta_store, DataStore& data_store)
    : meta_store_(meta_store), data_store_(data_store) {
}

Result<std::string> MultipartService::InitiateMultipartUpload(int64_t bucket_id, const std::string& key,
                                                               const std::string& content_type,
                                                               const std::string& owner_id) {
    // 生成 Upload ID
    std::string upload_id = UUID::Generate();
    
    // 计算过期时间（默认 7 天）
    auto expires_at = std::chrono::system_clock::now() + std::chrono::hours(24 * 7);
    
    // 创建 Multipart Upload 记录
    return meta_store_.CreateMultipartUpload(upload_id, bucket_id, key, content_type, owner_id, expires_at);
}

Result<std::string> MultipartService::UploadPart(const std::string& upload_id, int part_number,
                                                  const std::string& content) {
    // 验证 Upload 存在
    auto upload_result = GetMultipartUpload(upload_id);
    if (!upload_result.ok()) {
        return Result<std::string>::Err(upload_result.status());
    }
    
    // 验证 Part Number（1-10000）
    if (part_number < 1 || part_number > 10000) {
        return Result<std::string>::Err(Status::InvalidArgument("Part number must be between 1 and 10000"));
    }
    
    // 计算 ETag（SHA256）
    std::string sha256 = Crypto::SHA256(content);
    std::string etag = "\"" + sha256 + "\"";
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
    
    // 创建或更新 Part 记录
    auto part_result = meta_store_.CreateOrUpdatePart(upload_id, part_number, sha256, size, etag);
    if (!part_result.ok()) {
        return Result<std::string>::Err(part_result.status());
    }
    
    return Result<std::string>::Ok(etag);
}

Result<std::string> MultipartService::CompleteMultipartUpload(const std::string& upload_id,
                                                               const std::vector<std::pair<int, std::string>>& parts) {
    // 验证 Upload 存在
    auto upload_result = GetMultipartUpload(upload_id);
    if (!upload_result.ok()) {
        return Result<std::string>::Err(upload_result.status());
    }
    
    const auto& upload_info = upload_result.value();
    
    // 获取所有 Part
    auto parts_result = ListParts(upload_id);
    if (!parts_result.ok()) {
        return Result<std::string>::Err(parts_result.status());
    }
    
    const auto& stored_parts = parts_result.value().parts;
    
    // 验证提供的 Parts 与存储的 Parts 匹配
    if (parts.size() != stored_parts.size()) {
        return Result<std::string>::Err(Status::InvalidArgument("Part count mismatch"));
    }
    
    // 收集 CAS keys 按 Part Number 排序
    std::vector<std::string> cas_keys;
    for (const auto& [part_num, etag] : parts) {
        bool found = false;
        for (const auto& stored : stored_parts) {
            if (stored.part_number == part_num) {
                if (stored.etag != etag) {
                    return Result<std::string>::Err(
                        Status::InvalidArgument("ETag mismatch for part " + std::to_string(part_num)));
                }
                cas_keys.push_back(stored.sha256_hash);
                found = true;
                break;
            }
        }
        if (!found) {
            return Result<std::string>::Err(
                Status::NotFound("Part not found: " + std::to_string(part_num)));
        }
    }
    
    // 合并所有 Part 内容
    auto merge_result = data_store_.Merge(cas_keys);
    if (!merge_result.ok()) {
        return Result<std::string>::Err(merge_result.status());
    }
    
    std::string final_sha256 = merge_result.value();
    
    // 计算总大小
    int64_t total_size = 0;
    for (const auto& part : stored_parts) {
        total_size += part.size;
    }
    
    // 注册最终 CAS Blob
    auto cas_result = meta_store_.RegisterCasBlob(final_sha256, total_size);
    if (!cas_result.ok()) {
        return Result<std::string>::Err(cas_result.status());
    }
    
    // 创建对象
    std::string etag = "\"" + final_sha256 + "\"";
    auto put_result = meta_store_.PutObject(upload_info.bucket_id, upload_info.key,
                                            total_size, etag, upload_info.content_type,
                                            final_sha256, upload_info.owner_id);
    if (!put_result.ok()) {
        return Result<std::string>::Err(put_result.status());
    }
    
    // 完成分片上传（清理）
    auto complete_status = meta_store_.CompleteMultipartUpload(upload_id);
    if (!complete_status.ok()) {
        LOG_WARN("Failed to cleanup multipart upload: {}", complete_status.message());
    }
    
    return Result<std::string>::Ok(final_sha256);
}

Status MultipartService::AbortMultipartUpload(const std::string& upload_id) {
    // 获取所有 Part
    auto parts_result = ListParts(upload_id);
    if (parts_result.ok()) {
        // 减少每个 Part 的 CAS 引用计数
        for (const auto& part : parts_result.value().parts) {
            meta_store_.DecrementRefCount(part.sha256_hash);
        }
    }
    
    // 取消分片上传
    return meta_store_.AbortMultipartUpload(upload_id);
}

Result<ListPartsResult> MultipartService::ListParts(const std::string& upload_id,
                                                     int32_t part_number_marker,
                                                     int32_t max_parts) {
    return meta_store_.ListParts(upload_id, part_number_marker, max_parts);
}

Result<std::vector<MultipartUploadInfo>> MultipartService::ListMultipartUploads(int64_t bucket_id,
                                                                                 const std::string& prefix) {
    return meta_store_.ListMultipartUploads(bucket_id, prefix);
}

Result<MultipartUploadInfo> MultipartService::GetMultipartUpload(const std::string& upload_id) {
    return meta_store_.GetMultipartUpload(upload_id);
}

} // namespace minis3
