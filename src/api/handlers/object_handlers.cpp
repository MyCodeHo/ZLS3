#include "object_handlers.h"
#include "util/crypto.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <regex>

namespace minis3 {

using json = nlohmann::json;

HttpResponse ObjectHandlers::PutObject(HttpRequest& request, MetaStore& meta_store, DataStore& data_store,
                                       GarbageCollector* gc) {
    // 解析路径参数
    std::string bucket_name = request.GetPathParam("bucket");
    std::string object_key = request.GetPathParam("object");
    
    if (bucket_name.empty() || object_key.empty()) {
        return HttpResponse::BadRequest("Bucket and object key are required");
    }
    
    // 检查 bucket 是否存在
    auto bucket_result = meta_store.GetBucket(bucket_name);
    if (!bucket_result.ok()) {
        if (bucket_result.code() == ErrorCode::NoSuchBucket) {
            return HttpResponse::NotFound("Bucket not found");
        }
        return HttpResponse::InternalError("Failed to get bucket");
    }
    int64_t bucket_id = bucket_result.value().id;
    
    // 获取请求体（可能来自流式上传的内部 header）
    const std::string& body = request.Body();
        const std::string internal_cas = request.GetHeader("X-Internal-CAS-Key");
        const std::string internal_size = request.GetHeader("X-Internal-Size");
        if (body.empty() && internal_cas.empty()) {
            return HttpResponse::BadRequest("Request body is empty");
        }
    
    // 可选：校验 X-Content-SHA256
    std::string expected_sha256 = request.GetHeader("X-Content-SHA256");
    
    // 写入数据到 CAS（流式场景可直接复用内部 CAS）
        std::string cas_key;
        size_t data_size = body.size();
        if (!internal_cas.empty()) {
            cas_key = internal_cas;
            try {
                data_size = static_cast<size_t>(std::stoull(internal_size));
            } catch (...) {
                data_size = body.size();
            }
        } else {
            auto write_result = data_store.Write(body.data(), body.size());
            if (!write_result.ok()) {
                spdlog::error("Failed to write object data: {}", write_result.status().message());
                return HttpResponse::InternalError("Failed to write object data");
            }
            cas_key = write_result.value();
        }
    
    // 校验 SHA256（如果提供）
    if (!expected_sha256.empty() && expected_sha256 != cas_key) {
        // 删除刚写入的数据（不增加引用计数）
        spdlog::warn("Checksum mismatch: expected {}, got {}", expected_sha256, cas_key);
        return HttpResponse::BadRequest("Checksum mismatch");
    }

    // 幂等键处理（若提供）
    std::string idempotency_key = request.GetHeader("Idempotency-Key");
    std::string request_hash;
    if (!idempotency_key.empty()) {
        std::string method = HttpMethodToString(request.Method());
        std::string hash_input = method + "\n" + request.Path() + "\n" +
                                 std::to_string(data_size) + "\n" + expected_sha256;
        request_hash = Crypto::SHA256(hash_input);

        auto idem_result = meta_store.CheckIdempotency(idempotency_key);
        if (!idem_result.ok()) {
            return HttpResponse::InternalError("Failed to check idempotency");
        }
        if (idem_result.value().has_value()) {
            const auto& record = *idem_result.value();
            if (record.request_hash != request_hash) {
                return HttpResponse::Conflict("Idempotency conflict");
            }
            HttpResponse cached;
            cached.SetStatusCode(record.response_status);
            cached.SetHeader("Content-Type", "application/json");
            cached.SetBody(record.response_body);
            return cached;
        }
    }
    
    // 获取 content-type
    std::string content_type = request.GetHeader("Content-Type");
    if (content_type.empty()) {
        content_type = "application/octet-stream";
    }
    
    // 计算 ETag（使用 SHA256 的前 32 位）
    std::string etag_value = cas_key.substr(0, 32);
    std::string etag = "\"" + etag_value + "\"";
    
    // 获取所有者
    std::string owner_id = request.GetHeader("X-User-Id");
    if (owner_id.empty()) {
        owner_id = "anonymous";
    }
    
    // 创建/更新对象元数据（带 ref_count 事务）
    auto obj_result = meta_store.PutObjectWithRefCount(
        bucket_id,
        object_key,
            static_cast<int64_t>(data_size),
        etag,
        content_type,
        cas_key
    );
    
    if (!obj_result.ok()) {
        spdlog::error("Failed to put object metadata: {}", obj_result.status().message());
        return HttpResponse::InternalError("Failed to create object");
    }

    // 若旧对象释放，交由 GC 清理
    if (gc && obj_result.value().has_value()) {
        gc->AddPendingDelete(*obj_result.value());
    }
    
    // 构建响应
    HttpResponse response(HttpStatusCode::OK);
    response.SetHeader("ETag", etag);
    response.SetHeader("Content-Type", "application/json");
    
    json response_body = {
        {"bucket", bucket_name},
        {"key", object_key},
        {"etag", etag_value},
        {"size", data_size}
    };
    response.SetBody(response_body.dump());

    // 写入幂等性记录
    if (!idempotency_key.empty()) {
        meta_store.RecordIdempotency(idempotency_key, request_hash,
                                     response.StatusCode(), response.Body());
    }
    
    return response;
}

HttpResponse ObjectHandlers::GetObject(HttpRequest& request, MetaStore& meta_store, DataStore& data_store) {
    // 解析路径参数
    std::string bucket_name = request.GetPathParam("bucket");
    std::string object_key = request.GetPathParam("object");
    
    if (bucket_name.empty() || object_key.empty()) {
        return HttpResponse::BadRequest("Bucket and object key are required");
    }
    
    // 检查 bucket 是否存在
    auto bucket_result = meta_store.GetBucket(bucket_name);
    if (!bucket_result.ok()) {
        if (bucket_result.code() == ErrorCode::NoSuchBucket) {
            return HttpResponse::NotFound("Bucket not found");
        }
        return HttpResponse::InternalError("Failed to get bucket");
    }
    int64_t bucket_id = bucket_result.value().id;
    
    // 获取对象元数据
    auto obj_result = meta_store.GetObject(bucket_id, object_key);
    if (!obj_result.ok()) {
        if (obj_result.code() == ErrorCode::NoSuchKey) {
            return HttpResponse::NotFound("Object not found");
        }
        return HttpResponse::InternalError("Failed to get object");
    }
    
    const auto& obj = obj_result.value();
    
    // 检查 CAS 文件是否存在
    std::string file_path = data_store.GetFilePath(obj.sha256_hash);
    if (!data_store.Exists(obj.sha256_hash)) {
        spdlog::error("CAS file not found: {}", obj.sha256_hash);
        return HttpResponse::InternalError("Object data not found");
    }
    
    // 解析 Range 头
    std::string range_header = request.GetHeader("Range");
    size_t offset = 0;
    size_t length = obj.size;
    bool is_partial = false;
    
    if (!range_header.empty()) {
        auto range = ParseRange(range_header, obj.size);
        if (!range.has_value()) {
            return HttpResponse::RangeNotSatisfiable(obj.size);
        }
        offset = range->first;
        length = range->second - range->first + 1;
        is_partial = true;
    }
    
    // 构建响应
    HttpResponse response(is_partial ? HttpStatusCode::PARTIAL_CONTENT : HttpStatusCode::OK);
    response.SetHeader("Content-Type", obj.content_type);
    response.SetHeader("Content-Length", std::to_string(length));
    response.SetHeader("ETag", obj.etag);
    response.SetHeader("Accept-Ranges", "bytes");
    response.SetHeader("Last-Modified", std::to_string(
        std::chrono::system_clock::to_time_t(obj.created_at)));
    
    if (is_partial) {
        std::string content_range = "bytes " + std::to_string(offset) + "-" +
                                    std::to_string(offset + length - 1) + "/" +
                                    std::to_string(obj.size);
        response.SetHeader("Content-Range", content_range);
    }
    
    // 设置文件发送
    response.SetFile(file_path, obj.size, offset, length);
    
    return response;
}

HttpResponse ObjectHandlers::HeadObject(HttpRequest& request, MetaStore& meta_store) {
    // 解析路径参数
    std::string bucket_name = request.GetPathParam("bucket");
    std::string object_key = request.GetPathParam("object");
    
    if (bucket_name.empty() || object_key.empty()) {
        return HttpResponse::BadRequest("Bucket and object key are required");
    }
    
    // 检查 bucket 是否存在
    auto bucket_result = meta_store.GetBucket(bucket_name);
    if (!bucket_result.ok()) {
        if (bucket_result.code() == ErrorCode::NoSuchBucket) {
            return HttpResponse::NotFound("Bucket not found");
        }
        return HttpResponse::InternalError("Failed to get bucket");
    }
    int64_t bucket_id = bucket_result.value().id;
    
    // 获取对象元数据
    auto obj_result = meta_store.GetObject(bucket_id, object_key);
    if (!obj_result.ok()) {
        if (obj_result.code() == ErrorCode::NoSuchKey) {
            return HttpResponse::NotFound("Object not found");
        }
        return HttpResponse::InternalError("Failed to get object");
    }
    
    const auto& obj = obj_result.value();
    
    // 构建响应（HEAD 请求不返回 body）
    HttpResponse response(HttpStatusCode::OK);
    response.SetHeader("Content-Type", obj.content_type);
    response.SetHeader("Content-Length", std::to_string(obj.size));
    response.SetHeader("ETag", obj.etag);
    response.SetHeader("Accept-Ranges", "bytes");
    response.SetHeader("Last-Modified", std::to_string(
        std::chrono::system_clock::to_time_t(obj.created_at)));
    
    return response;
}

HttpResponse ObjectHandlers::DeleteObject(HttpRequest& request, MetaStore& meta_store, DataStore& data_store,
                                          GarbageCollector* gc) {
    // 解析路径参数
    std::string bucket_name = request.GetPathParam("bucket");
    std::string object_key = request.GetPathParam("object");
    
    if (bucket_name.empty() || object_key.empty()) {
        return HttpResponse::BadRequest("Bucket and object key are required");
    }
    
    // 检查 bucket 是否存在
    auto bucket_result = meta_store.GetBucket(bucket_name);
    if (!bucket_result.ok()) {
        if (bucket_result.code() == ErrorCode::NoSuchBucket) {
            return HttpResponse::NotFound("Bucket not found");
        }
        return HttpResponse::InternalError("Failed to get bucket");
    }
    int64_t bucket_id = bucket_result.value().id;
    
    // 获取对象（用于获取 CAS key）
    auto obj_result = meta_store.GetObject(bucket_id, object_key);
    if (!obj_result.ok()) {
        if (obj_result.code() == ErrorCode::NoSuchKey) {
            return HttpResponse::NotFound("Object not found");
        }
        return HttpResponse::InternalError("Failed to get object");
    }
    
    // 删除对象元数据（带 ref_count 事务）
    auto del_result = meta_store.DeleteObjectWithRefCount(bucket_id, object_key);
    if (!del_result.ok()) {
        spdlog::error("Failed to delete object: {}", del_result.status().message());
        return HttpResponse::InternalError("Failed to delete object");
    }

    // 若 CAS 引用为 0，交给 GC 处理
    if (gc && del_result.value().has_value()) {
        gc->AddPendingDelete(*del_result.value());
    }
    
    return HttpResponse::NoContent();
}

HttpResponse ObjectHandlers::ListObjects(HttpRequest& request, MetaStore& meta_store) {
    // 解析路径参数
    std::string bucket_name = request.GetPathParam("bucket");
    
    if (bucket_name.empty()) {
        return HttpResponse::BadRequest("Bucket name is required");
    }
    
    // 检查 bucket 是否存在
    auto bucket_result = meta_store.GetBucket(bucket_name);
    if (!bucket_result.ok()) {
        if (bucket_result.code() == ErrorCode::NoSuchBucket) {
            return HttpResponse::NotFound("Bucket not found");
        }
        return HttpResponse::InternalError("Failed to get bucket");
    }
    int64_t bucket_id = bucket_result.value().id;
    
    // 获取查询参数
    std::string prefix = request.GetQueryParam("prefix");
    std::string delimiter = request.GetQueryParam("delimiter");
    std::string start_after = request.GetQueryParam("start-after");
    int max_keys = 1000;
    
    std::string max_keys_str = request.GetQueryParam("max-keys");
    if (!max_keys_str.empty()) {
        try {
            max_keys = std::stoi(max_keys_str);
            if (max_keys < 1 || max_keys > 1000) {
                max_keys = 1000;
            }
        } catch (...) {
            max_keys = 1000;
        }
    }
    
    // 列出对象
    auto result = meta_store.ListObjects(bucket_id, prefix, delimiter, start_after, max_keys);
    if (!result.ok()) {
        spdlog::error("Failed to list objects: {}", result.status().message());
        return HttpResponse::InternalError("Failed to list objects");
    }
    
    const auto& list_result = result.value();
    
    // 构建响应
    json objects_array = json::array();
    for (const auto& obj : list_result.objects) {
        objects_array.push_back({
            {"key", obj.key},
            {"size", obj.size},
            {"etag", obj.etag},
            {"last_modified", std::chrono::system_clock::to_time_t(obj.created_at)}
        });
    }
    
    json response_body = {
        {"name", bucket_name},
        {"prefix", prefix},
        {"delimiter", delimiter},
        {"max_keys", max_keys},
        {"is_truncated", list_result.is_truncated},
        {"contents", objects_array},
        {"common_prefixes", list_result.common_prefixes}
    };
    
    if (!list_result.next_continuation_token.empty()) {
        response_body["next_continuation_token"] = list_result.next_continuation_token;
    }
    
    return HttpResponse::OK(response_body.dump());
}

std::optional<std::pair<size_t, size_t>> ObjectHandlers::ParseRange(
    const std::string& range_header, size_t file_size) {
    
    // 格式: bytes=start-end
    static std::regex range_regex(R"(bytes=(\d*)-(\d*))");
    std::smatch match;
    
    if (!std::regex_match(range_header, match, range_regex)) {
        return std::nullopt;
    }
    
    std::string start_str = match[1].str();
    std::string end_str = match[2].str();
    
    size_t start = 0;
    size_t end = file_size - 1;
    
    if (start_str.empty() && end_str.empty()) {
        return std::nullopt;
    }
    
    if (start_str.empty()) {
        // bytes=-N (最后 N 字节)
        size_t suffix_length = std::stoull(end_str);
        if (suffix_length > file_size) {
            suffix_length = file_size;
        }
        start = file_size - suffix_length;
        end = file_size - 1;
    } else if (end_str.empty()) {
        // bytes=N- (从 N 开始到结束)
        start = std::stoull(start_str);
        if (start >= file_size) {
            return std::nullopt;
        }
    } else {
        // bytes=N-M
        start = std::stoull(start_str);
        end = std::stoull(end_str);
        if (start > end || start >= file_size) {
            return std::nullopt;
        }
        if (end >= file_size) {
            end = file_size - 1;
        }
    }
    
    return std::make_pair(start, end);
}

} // namespace minis3
