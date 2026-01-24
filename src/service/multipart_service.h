#pragma once

#include "db/meta_store.h"
#include "storage/data_store.h"
#include "util/status.h"
#include <string>
#include <vector>
#include <cstdint>

namespace minis3 {

// 使用 db/meta_store.h 中定义的 MultipartUploadInfo 和 MultipartPartInfo

/**
 * Multipart 上传服务层
 */
class MultipartService {
public:
    MultipartService(MetaStore& meta_store, DataStore& data_store);
    
    /**
     * 初始化 Multipart Upload
     */
    Result<std::string> InitiateMultipartUpload(int64_t bucket_id, const std::string& key,
                                                 const std::string& content_type,
                                                 const std::string& owner_id);
    
    /**
     * 上传 Part
     */
    Result<std::string> UploadPart(const std::string& upload_id, int part_number,
                                    const std::string& content);
    
    /**
     * 完成 Multipart Upload
     */
    Result<std::string> CompleteMultipartUpload(const std::string& upload_id,
                                                 const std::vector<std::pair<int, std::string>>& parts);
    
    /**
     * 中止 Multipart Upload
     */
    Status AbortMultipartUpload(const std::string& upload_id);
    
    /**
     * 列出 Parts
     */
    Result<ListPartsResult> ListParts(const std::string& upload_id,
                                       int32_t part_number_marker = 0,
                                       int32_t max_parts = 1000);
    
    /**
     * 列出 Multipart Uploads
     */
    Result<std::vector<MultipartUploadInfo>> ListMultipartUploads(int64_t bucket_id,
                                                                   const std::string& prefix = "");
    
    /**
     * 获取 Multipart Upload 信息
     */
    Result<MultipartUploadInfo> GetMultipartUpload(const std::string& upload_id);

private:
    MetaStore& meta_store_;
    DataStore& data_store_;
};

} // namespace minis3
