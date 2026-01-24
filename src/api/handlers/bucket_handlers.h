#pragma once

#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "db/meta_store.h"

namespace minis3 {

/**
 * Bucket 相关的 HTTP 处理函数
 */
class BucketHandlers {
public:
    /**
     * PUT /buckets/{bucket}
     * 创建 Bucket
     */
    static HttpResponse CreateBucket(HttpRequest& request, MetaStore& meta_store);
    
    /**
     * GET /buckets/{bucket}
     * 获取 Bucket 信息
     */
    static HttpResponse GetBucket(HttpRequest& request, MetaStore& meta_store);
    
    /**
     * DELETE /buckets/{bucket}
     * 删除 Bucket
     */
    static HttpResponse DeleteBucket(HttpRequest& request, MetaStore& meta_store);
    
    /**
     * GET /buckets
     * 列出所有 Bucket
     */
    static HttpResponse ListBuckets(HttpRequest& request, MetaStore& meta_store);
};

} // namespace minis3
