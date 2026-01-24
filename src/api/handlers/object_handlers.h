#pragma once

#include "net/http/http_request.h"
#include "net/http/http_response.h"
#include "db/meta_store.h"
#include "storage/data_store.h"
#include "storage/gc.h"

namespace minis3 {

/**
 * Object 相关的 HTTP 处理函数
 */
class ObjectHandlers {
public:
    /**
     * PUT /buckets/{bucket}/objects/{object}
     * 上传对象
     */
    static HttpResponse PutObject(HttpRequest& request, MetaStore& meta_store, DataStore& data_store,
                                  GarbageCollector* gc);
    
    /**
     * GET /buckets/{bucket}/objects/{object}
     * 下载对象（支持 Range）
     */
    static HttpResponse GetObject(HttpRequest& request, MetaStore& meta_store, DataStore& data_store);
    
    /**
     * HEAD /buckets/{bucket}/objects/{object}
     * 获取对象元数据
     */
    static HttpResponse HeadObject(HttpRequest& request, MetaStore& meta_store);
    
    /**
     * DELETE /buckets/{bucket}/objects/{object}
     * 删除对象
     */
    static HttpResponse DeleteObject(HttpRequest& request, MetaStore& meta_store, DataStore& data_store,
                                     GarbageCollector* gc);
    
    /**
     * GET /buckets/{bucket}/objects
     * 列出对象
     */
    static HttpResponse ListObjects(HttpRequest& request, MetaStore& meta_store);

private:
    /**
     * 解析 Range 头
     * @return {start, end} 或 nullopt
     */
    static std::optional<std::pair<size_t, size_t>> ParseRange(
        const std::string& range_header, size_t file_size);
};

} // namespace minis3
