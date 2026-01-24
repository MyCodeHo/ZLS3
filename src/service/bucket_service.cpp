#include "bucket_service.h"
#include "util/logging.h"

namespace minis3 {

BucketService::BucketService(MetaStore& meta_store)
    : meta_store_(meta_store) {
}

Result<int64_t> BucketService::CreateBucket(const std::string& bucket_name, const std::string& owner_id,
                                             const std::string& region) {
    // 检查是否已存在
    auto exists_result = BucketExists(bucket_name);
    if (!exists_result.ok()) {
        return Result<int64_t>::Err(exists_result.status());
    }
    
    if (exists_result.value()) {
        return Result<int64_t>::Err(Status::AlreadyExists("Bucket already exists: " + bucket_name));
    }
    
    // 创建 Bucket
    return meta_store_.CreateBucket(bucket_name, owner_id, region);
}

Status BucketService::DeleteBucket(const std::string& bucket_name) {
    // 获取 Bucket
    auto bucket_result = GetBucket(bucket_name);
    if (!bucket_result.ok()) {
        return bucket_result.status();
    }
    
    // 检查是否为空
    auto empty_result = meta_store_.IsBucketEmpty(bucket_result.value().id);
    if (!empty_result.ok()) {
        return empty_result.status();
    }
    
    if (!empty_result.value()) {
        return Status::InvalidArgument("Bucket is not empty: " + bucket_name);
    }
    
    // 删除 Bucket
    return meta_store_.DeleteBucket(bucket_name);
}

Result<bool> BucketService::BucketExists(const std::string& bucket_name) {
    // 通过查询判断是否存在
    auto result = meta_store_.GetBucket(bucket_name);
    if (result.ok()) {
        return Result<bool>::Ok(true);
    }
    if (result.status().IsNotFound()) {
        return Result<bool>::Ok(false);
    }
    return Result<bool>::Err(result.status());
}

Result<BucketInfo> BucketService::GetBucket(const std::string& bucket_name) {
    return meta_store_.GetBucket(bucket_name);
}

Result<std::vector<BucketInfo>> BucketService::ListBuckets(const std::string& owner_id) {
    return meta_store_.ListBuckets(owner_id);
}

} // namespace minis3
