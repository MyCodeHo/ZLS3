#pragma once

#include "db/meta_store.h"
#include "storage/data_store.h"
#include "util/status.h"
#include <string>
#include <vector>
#include <functional>

namespace minis3 {

// 使用 db/meta_store.h 中定义的 ObjectInfo

/**
 * Object 服务层
 */
class ObjectService {
public:
    ObjectService(MetaStore& meta_store, DataStore& data_store);
    
    /**
     * 上传对象（PUT）
     */
    Result<std::string> PutObject(int64_t bucket_id, const std::string& key,
                                   const std::string& content, const std::string& content_type,
                                   const std::string& owner_id);
    
    /**
     * 获取对象
     */
    Result<std::pair<std::string, ObjectInfo>> GetObject(int64_t bucket_id, const std::string& key);
    
    /**
     * 获取对象的 Range
     */
    Result<std::pair<std::string, ObjectInfo>> GetObjectRange(int64_t bucket_id, const std::string& key,
                                                               int64_t start, int64_t end);
    
    /**
     * 删除对象
     */
    Status DeleteObject(int64_t bucket_id, const std::string& key);
    
    /**
     * 检查对象是否存在
     */
    Result<bool> ObjectExists(int64_t bucket_id, const std::string& key);
    
    /**
     * 获取对象信息（HEAD）
     */
    Result<ObjectInfo> HeadObject(int64_t bucket_id, const std::string& key);
    
    /**
     * 列出对象
     */
    Result<ListObjectsResult> ListObjects(int64_t bucket_id, const std::string& prefix,
                                          const std::string& delimiter,
                                          const std::string& start_after, int limit);
    
    /**
     * 复制对象
     */
    Result<std::string> CopyObject(int64_t src_bucket_id, const std::string& src_key,
                                    int64_t dst_bucket_id, const std::string& dst_key,
                                    const std::string& owner_id);

private:
    MetaStore& meta_store_;
    DataStore& data_store_;
};

} // namespace minis3
