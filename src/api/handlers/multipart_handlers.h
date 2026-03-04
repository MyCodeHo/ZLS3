#pragma once

#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "db/meta_store.h"
#include "storage/data_store.h"
#include "storage/gc.h"

namespace minis3 {

/**
 * Multipart 上传相关的 HTTP 处理函数
 */
class MultipartHandlers {
public:
    /**
     * POST /buckets/{bucket}/multipart/{object}
     * 初始化 multipart 上传
     */
    static HttpResponse InitUpload(HttpRequest& request, MetaStore& meta_store);
    
    /**
     * PUT /buckets/{bucket}/multipart/{object}/{upload_id}/parts/{part_number}
     * 上传分片
     */
    static HttpResponse UploadPart(HttpRequest& request, MetaStore& meta_store, DataStore& data_store,
                                   GarbageCollector* gc);
    
    /**
     * POST /buckets/{bucket}/multipart/{object}/{upload_id}/complete
     * 完成 multipart 上传
     */
    static HttpResponse CompleteUpload(HttpRequest& request, MetaStore& meta_store, DataStore& data_store,
                                       GarbageCollector* gc);
    
    /**
     * DELETE /buckets/{bucket}/multipart/{object}/{upload_id}
     * 取消 multipart 上传
     */
    static HttpResponse AbortUpload(HttpRequest& request, MetaStore& meta_store, DataStore& data_store,
                                    GarbageCollector* gc);
    
    /**
     * GET /buckets/{bucket}/multipart/{object}/{upload_id}/parts
     * 列出已上传的分片
     */
    static HttpResponse ListParts(HttpRequest& request, MetaStore& meta_store);
};

} // namespace minis3
