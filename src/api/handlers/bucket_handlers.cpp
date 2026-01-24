#include "bucket_handlers.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace minis3 {

using json = nlohmann::json;

HttpResponse BucketHandlers::CreateBucket(HttpRequest& request, MetaStore& meta_store) {
    std::string bucket_name = request.GetPathParam("bucket");
    
    if (bucket_name.empty()) {
        return HttpResponse::BadRequest("Bucket name is required");
    }
    
        // 解析路径参数
    // 验证 bucket 名称
    if (bucket_name.length() < 3 || bucket_name.length() > 63) {
        return HttpResponse::BadRequest("Bucket name must be between 3 and 63 characters");
    }
    
    // 获取所有者 ID（从认证中间件获取）
    std::string owner_id = request.GetHeader("X-User-Id");
    if (owner_id.empty()) {
        owner_id = "anonymous";
    }
    
    // 创建 bucket
    auto result = meta_store.CreateBucket(bucket_name, owner_id);
    
    if (!result.ok()) {
        if (result.code() == ErrorCode::BucketAlreadyExists) {
            return HttpResponse::Conflict("Bucket already exists");
        }
        spdlog::error("Failed to create bucket {}: {}", bucket_name, result.status().message());
        return HttpResponse::InternalError("Failed to create bucket");
    }
    
    json response_body = {
        {"bucket", bucket_name},
        {"created", true}
    };
    
    return HttpResponse::Created(response_body.dump());
}

HttpResponse BucketHandlers::GetBucket(HttpRequest& request, MetaStore& meta_store) {
    std::string bucket_name = request.GetPathParam("bucket");
    
    if (bucket_name.empty()) {
        return HttpResponse::BadRequest("Bucket name is required");
    }
    
        // 解析路径参数
    auto result = meta_store.GetBucket(bucket_name);
    
    if (!result.ok()) {
        if (result.code() == ErrorCode::NoSuchBucket) {
            return HttpResponse::NotFound("Bucket not found");
        }
        spdlog::error("Failed to get bucket {}: {}", bucket_name, result.status().message());
        return HttpResponse::InternalError("Failed to get bucket");
    }
    
    const auto& bucket = result.value();
    
    json response_body = {
        {"name", bucket.name},
        {"owner_id", bucket.owner_id},
        {"region", bucket.region},
        {"created_at", std::chrono::system_clock::to_time_t(bucket.created_at)}
    };
    
    return HttpResponse::OK(response_body.dump());
}

HttpResponse BucketHandlers::DeleteBucket(HttpRequest& request, MetaStore& meta_store) {
    std::string bucket_name = request.GetPathParam("bucket");
    
    if (bucket_name.empty()) {
        return HttpResponse::BadRequest("Bucket name is required");
    }
    
        // 解析路径参数
    // 先检查 bucket 是否存在
    auto bucket_result = meta_store.GetBucket(bucket_name);
    if (!bucket_result.ok()) {
        if (bucket_result.code() == ErrorCode::NoSuchBucket) {
            return HttpResponse::NotFound("Bucket not found");
        }
        return HttpResponse::InternalError("Failed to get bucket");
    }
    
    // 检查 bucket 是否为空
    auto empty_result = meta_store.IsBucketEmpty(bucket_result.value().id);
    if (!empty_result.ok()) {
        return HttpResponse::InternalError("Failed to check bucket");
    }
    
    if (!empty_result.value()) {
        return HttpResponse::Conflict("Bucket is not empty");
    }
    
    // 删除 bucket
    auto status = meta_store.DeleteBucket(bucket_name);
    if (!status.ok()) {
        spdlog::error("Failed to delete bucket {}: {}", bucket_name, status.message());
        return HttpResponse::InternalError("Failed to delete bucket");
    }
    
    return HttpResponse::NoContent();
}

HttpResponse BucketHandlers::ListBuckets(HttpRequest& request, MetaStore& meta_store) {
    std::string owner_id = request.GetHeader("X-User-Id");
    if (owner_id.empty()) {
        owner_id = "anonymous";
    }
    
        // 获取 owner_id（可能来自认证中间件）
    auto result = meta_store.ListBuckets(owner_id);
    
    if (!result.ok()) {
        spdlog::error("Failed to list buckets: {}", result.status().message());
        return HttpResponse::InternalError("Failed to list buckets");
    }
    
    json buckets_array = json::array();
    for (const auto& bucket : result.value()) {
        buckets_array.push_back({
            {"name", bucket.name},
            {"created_at", std::chrono::system_clock::to_time_t(bucket.created_at)}
        });
    }
    
    json response_body = {
        {"buckets", buckets_array}
    };
    
    return HttpResponse::OK(response_body.dump());
}

} // namespace minis3
