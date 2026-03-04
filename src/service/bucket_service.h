#pragma once

#include "db/meta_store.h"
#include "util/status.h"
#include <string>
#include <vector>

namespace minis3 {

// 使用 db/meta_store.h 中定义的 BucketInfo

/**
 * Bucket 服务层
 */
class BucketService {
public:
    explicit BucketService(MetaStore& meta_store);
    
    /**
     * 创建 Bucket
     */
    Result<int64_t> CreateBucket(const std::string& bucket_name, const std::string& owner_id,
                                  const std::string& region = "default");
    
    /**
     * 删除 Bucket（仅当为空时）
     */
    Status DeleteBucket(const std::string& bucket_name);
    
    /**
     * 检查 Bucket 是否存在
     */
    Result<bool> BucketExists(const std::string& bucket_name);
    
    /**
     * 获取 Bucket 信息
     */
    Result<BucketInfo> GetBucket(const std::string& bucket_name);
    
    /**
     * 列出所有 Bucket
     */
    Result<std::vector<BucketInfo>> ListBuckets(const std::string& owner_id);

private:
    MetaStore& meta_store_;
};

} // namespace minis3
