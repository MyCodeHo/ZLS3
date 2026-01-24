#include "multipart_handlers.h"
#include "util/uuid.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>

namespace minis3 {

using json = nlohmann::json;

namespace {

std::string NormalizeEtagValue(const std::string& etag) {
    // 去除双引号，统一 ETag 比较格式
    if (etag.size() >= 2 && etag.front() == '"' && etag.back() == '"') {
        return etag.substr(1, etag.size() - 2);
    }
    return etag;
}

} // namespace

HttpResponse MultipartHandlers::InitUpload(HttpRequest& request, MetaStore& meta_store) {
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
    
    // 生成 upload_id
    std::string upload_id = UUID::Generate();
    
    // 获取 content-type
    std::string content_type = request.GetHeader("Content-Type");
    if (content_type.empty()) {
        content_type = "application/octet-stream";
    }
    
    // 获取所有者
    std::string owner_id = request.GetHeader("X-User-Id");
    if (owner_id.empty()) {
        owner_id = "anonymous";
    }
    
    // 计算过期时间（24小时后）
    auto expires_at = std::chrono::system_clock::now() + std::chrono::hours(24);
    
    // 创建 multipart 上传记录
    auto result = meta_store.CreateMultipartUpload(
        upload_id,
        bucket_id,
        object_key,
        content_type,
        owner_id,
        expires_at
    );
    
    if (!result.ok()) {
        spdlog::error("Failed to create multipart upload: {}", result.status().message());
        return HttpResponse::InternalError("Failed to initiate upload");
    }
    
    // 构建响应
    json response_body = {
        {"upload_id", upload_id},
        {"bucket", bucket_name},
        {"key", object_key},
        {"expires_at", std::chrono::system_clock::to_time_t(expires_at)}
    };
    
    return HttpResponse::OK(response_body.dump());
}

HttpResponse MultipartHandlers::UploadPart(HttpRequest& request, MetaStore& meta_store, DataStore& data_store,
                                           GarbageCollector* gc) {
    // 解析路径参数
    std::string bucket_name = request.GetPathParam("bucket");
    std::string object_key = request.GetPathParam("object");
    std::string upload_id = request.GetPathParam("upload_id");
    std::string part_number_str = request.GetPathParam("part_number");
    
    if (bucket_name.empty() || object_key.empty() || upload_id.empty() || part_number_str.empty()) {
        return HttpResponse::BadRequest("Missing required parameters");
    }
    
    int part_number;
    try {
        part_number = std::stoi(part_number_str);
    } catch (...) {
        return HttpResponse::BadRequest("Invalid part number");
    }
    
    if (part_number < 1 || part_number > 10000) {
        return HttpResponse::BadRequest("Part number must be between 1 and 10000");
    }
    
    // 检查 upload_id 是否存在
    auto upload_result = meta_store.GetMultipartUpload(upload_id);
    if (!upload_result.ok()) {
        if (upload_result.code() == ErrorCode::NoSuchUpload) {
            return HttpResponse::NotFound("Upload not found");
        }
        return HttpResponse::InternalError("Failed to get upload");
    }
    
    // 验证 bucket 和 object_key
    const auto& upload = upload_result.value();
    auto bucket_result = meta_store.GetBucketById(upload.bucket_id);
    if (!bucket_result.ok() || bucket_result.value().name != bucket_name) {
        return HttpResponse::BadRequest("Upload ID does not match bucket");
    }
    
    if (upload.key != object_key) {
        return HttpResponse::BadRequest("Upload ID does not match object key");
    }
    
    const std::string internal_cas = request.GetHeader("X-Internal-CAS-Key");
    const std::string internal_size = request.GetHeader("X-Internal-Size");
    const std::string& body = request.Body();
    if (body.empty() && internal_cas.empty()) {
        return HttpResponse::BadRequest("Request body is empty");
    }
    
    // 检查最小分片大小（最后一个分片除外）
    // 这里简化处理，不严格检查是否是最后一个分片
    
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
        // 写入数据到 CAS
        auto write_result = data_store.Write(body.data(), body.size());
        if (!write_result.ok()) {
            spdlog::error("Failed to write part data: {}", write_result.status().message());
            return HttpResponse::InternalError("Failed to write part data");
        }
        cas_key = write_result.value();
    }
    
    // 计算 ETag
    std::string etag_value = cas_key.substr(0, 32);
    std::string etag = "\"" + etag_value + "\"";
    
    // 创建/更新分片记录（带 ref_count 事务）
    auto part_result = meta_store.PutPartWithRefCount(
        upload_id,
        part_number,
        cas_key,
        static_cast<int64_t>(data_size),
        etag
    );
    
    if (!part_result.ok()) {
        spdlog::error("Failed to create part record: {}", part_result.status().message());
        return HttpResponse::InternalError("Failed to create part");
    }

    // 若旧分片释放，交给 GC
    if (gc && part_result.value().has_value()) {
        gc->AddPendingDelete(*part_result.value());
    }
    
    // 构建响应
    json response_body = {
        {"etag", etag_value},
        {"part_number", part_number},
        {"size", data_size}
    };
    
    HttpResponse response(HttpStatusCode::OK);
    response.SetHeader("ETag", etag);
    response.SetBody(response_body.dump());
    
    return response;
}

HttpResponse MultipartHandlers::CompleteUpload(HttpRequest& request, MetaStore& meta_store, DataStore& data_store,
                                               GarbageCollector* gc) {
    // 解析路径参数
    std::string bucket_name = request.GetPathParam("bucket");
    std::string object_key = request.GetPathParam("object");
    std::string upload_id = request.GetPathParam("upload_id");
    
    if (bucket_name.empty() || object_key.empty() || upload_id.empty()) {
        return HttpResponse::BadRequest("Missing required parameters");
    }
    
    // 检查 upload_id 是否存在
    auto upload_result = meta_store.GetMultipartUpload(upload_id);
    if (!upload_result.ok()) {
        if (upload_result.code() == ErrorCode::NoSuchUpload) {
            return HttpResponse::NotFound("Upload not found");
        }
        return HttpResponse::InternalError("Failed to get upload");
    }
    
    const auto& upload = upload_result.value();
    
    // 解析请求体中的 parts 列表
    json request_body;
    try {
        request_body = json::parse(request.Body());
    } catch (...) {
        return HttpResponse::BadRequest("Invalid JSON body");
    }
    
    if (!request_body.contains("parts") || !request_body["parts"].is_array()) {
        return HttpResponse::BadRequest("Missing parts array");
    }
    
    std::vector<std::pair<int, std::string>> client_parts;  // {part_number, etag}
    for (const auto& part : request_body["parts"]) {
        if (!part.contains("part_number") || !part.contains("etag")) {
            return HttpResponse::BadRequest("Invalid part format");
        }
        client_parts.emplace_back(
            part["part_number"].get<int>(),
            NormalizeEtagValue(part["etag"].get<std::string>())
        );
    }
    
    // 按 part_number 排序
    std::sort(client_parts.begin(), client_parts.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    
    // 获取服务端存储的所有分片
    auto parts_result = meta_store.ListParts(upload_id, 0, 10000);
    if (!parts_result.ok()) {
        return HttpResponse::InternalError("Failed to list parts");
    }
    
    const auto& server_parts = parts_result.value().parts;
    
    // 验证分片数量和顺序
    if (client_parts.size() != server_parts.size()) {
        return HttpResponse::BadRequest("Part count mismatch");
    }
    
    // 收集 CAS keys 用于合并
    std::vector<std::string> cas_keys;
    size_t total_size = 0;
    
    for (size_t i = 0; i < client_parts.size(); ++i) {
        if (client_parts[i].first != server_parts[i].part_number) {
            return HttpResponse::BadRequest("Part number mismatch");
        }
        if (client_parts[i].second != NormalizeEtagValue(server_parts[i].etag)) {
            return HttpResponse::BadRequest("Part ETag mismatch for part " + 
                                            std::to_string(client_parts[i].first));
        }
        cas_keys.push_back(server_parts[i].sha256_hash);
        total_size += server_parts[i].size;
    }
    
    // 合并所有分片生成最终对象
    auto merge_result = data_store.Merge(cas_keys);
    if (!merge_result.ok()) {
        spdlog::error("Failed to merge parts: {}", merge_result.status().message());
        return HttpResponse::InternalError("Failed to merge parts");
    }
    
    std::string final_cas_key = merge_result.value();
    std::string final_etag_value = final_cas_key.substr(0, 32);
    std::string final_etag = "\"" + final_etag_value + "\"";
    
    // 创建对象（带 ref_count 事务）
    auto obj_result = meta_store.PutObjectWithRefCount(
        upload.bucket_id,
        upload.key,
        static_cast<int64_t>(total_size),
        final_etag,
        upload.content_type,
        final_cas_key
    );
    
    if (!obj_result.ok()) {
        spdlog::error("Failed to create object: {}", obj_result.status().message());
        return HttpResponse::InternalError("Failed to create object");
    }

    // 若旧对象释放，交给 GC
    if (gc && obj_result.value().has_value()) {
        gc->AddPendingDelete(*obj_result.value());
    }
    
    // 清理分片的引用计数
    for (const auto& cas_key : cas_keys) {
        auto dec_result = meta_store.DecrementRefCountAndCheckZero(cas_key);
        if (dec_result.ok() && dec_result.value() && gc) {
            gc->AddPendingDelete(cas_key);
        }
    }
    
    // 删除 multipart 上传记录（会级联删除 parts）
    meta_store.DeleteMultipartUpload(upload_id);
    
    // 构建响应
    json response_body = {
        {"bucket", bucket_name},
        {"key", object_key},
        {"etag", final_etag_value},
        {"size", total_size}
    };
    
    HttpResponse response(HttpStatusCode::OK);
    response.SetHeader("ETag", final_etag);
    response.SetBody(response_body.dump());
    return response;
}

HttpResponse MultipartHandlers::AbortUpload(HttpRequest& request, MetaStore& meta_store, DataStore& data_store,
                                            GarbageCollector* gc) {
    // 解析路径参数
    std::string bucket_name = request.GetPathParam("bucket");
    std::string object_key = request.GetPathParam("object");
    std::string upload_id = request.GetPathParam("upload_id");
    
    if (bucket_name.empty() || object_key.empty() || upload_id.empty()) {
        return HttpResponse::BadRequest("Missing required parameters");
    }
    
    // 检查 upload_id 是否存在
    auto upload_result = meta_store.GetMultipartUpload(upload_id);
    if (!upload_result.ok()) {
        if (upload_result.code() == ErrorCode::NoSuchUpload) {
            return HttpResponse::NotFound("Upload not found");
        }
        return HttpResponse::InternalError("Failed to get upload");
    }
    
    // 获取所有分片，减少引用计数
    auto parts_result = meta_store.ListParts(upload_id, 0, 10000);
    if (parts_result.ok()) {
        for (const auto& part : parts_result.value().parts) {
            auto dec_result = meta_store.DecrementRefCountAndCheckZero(part.sha256_hash);
            if (dec_result.ok() && dec_result.value() && gc) {
                gc->AddPendingDelete(part.sha256_hash);
            }
        }
    }
    
    // 删除 multipart 上传记录
    auto status = meta_store.DeleteMultipartUpload(upload_id);
    if (!status.ok()) {
        spdlog::error("Failed to delete multipart upload: {}", status.message());
        return HttpResponse::InternalError("Failed to abort upload");
    }
    
    return HttpResponse::NoContent();
}

HttpResponse MultipartHandlers::ListParts(HttpRequest& request, MetaStore& meta_store) {
    // 解析路径参数
    std::string bucket_name = request.GetPathParam("bucket");
    std::string object_key = request.GetPathParam("object");
    std::string upload_id = request.GetPathParam("upload_id");
    
    if (bucket_name.empty() || object_key.empty() || upload_id.empty()) {
        return HttpResponse::BadRequest("Missing required parameters");
    }
    
    // 检查 upload_id 是否存在
    auto upload_result = meta_store.GetMultipartUpload(upload_id);
    if (!upload_result.ok()) {
        if (upload_result.code() == ErrorCode::NoSuchUpload) {
            return HttpResponse::NotFound("Upload not found");
        }
        return HttpResponse::InternalError("Failed to get upload");
    }
    
    // 获取查询参数
    int part_number_marker = 0;
    int max_parts = 1000;
    
    std::string marker_str = request.GetQueryParam("part-number-marker");
    if (!marker_str.empty()) {
        try {
            part_number_marker = std::stoi(marker_str);
        } catch (...) {}
    }
    
    std::string max_parts_str = request.GetQueryParam("max-parts");
    if (!max_parts_str.empty()) {
        try {
            max_parts = std::stoi(max_parts_str);
            if (max_parts < 1 || max_parts > 1000) {
                max_parts = 1000;
            }
        } catch (...) {}
    }
    
    // 列出分片
    auto result = meta_store.ListParts(upload_id, part_number_marker, max_parts);
    if (!result.ok()) {
        spdlog::error("Failed to list parts: {}", result.status().message());
        return HttpResponse::InternalError("Failed to list parts");
    }
    
    const auto& list_result = result.value();
    
    // 构建响应
    json parts_array = json::array();
    for (const auto& part : list_result.parts) {
        parts_array.push_back({
            {"part_number", part.part_number},
            {"etag", NormalizeEtagValue(part.etag)},
            {"size", part.size},
            {"last_modified", std::chrono::system_clock::to_time_t(part.created_at)}
        });
    }
    
    json response_body = {
        {"bucket", bucket_name},
        {"key", object_key},
        {"upload_id", upload_id},
        {"parts", parts_array},
        {"is_truncated", list_result.is_truncated},
        {"max_parts", max_parts}
    };
    
    if (list_result.is_truncated) {
        response_body["next_part_number_marker"] = list_result.next_part_number_marker;
    }
    
    return HttpResponse::OK(response_body.dump());
}

} // namespace minis3
