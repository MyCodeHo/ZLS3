#include "meta_store.h"
#include "../util/logging.h"
#include "../util/uuid.h"
#include "mysql_tx.h"
#include <sstream>
#include <iomanip>

namespace minis3 {

namespace {
std::chrono::system_clock::time_point ParseMysqlTime(const char* str) {
    // 解析 MySQL DATETIME 字符串为 time_point
    if (!str) {
        return std::chrono::system_clock::now();
    }
    std::tm tm = {};
    std::istringstream ss(str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        return std::chrono::system_clock::now();
    }
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::string TimePointToMySQL(const std::chrono::system_clock::time_point& tp) {
    // time_point -> MySQL DATETIME 字符串
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::gmtime(&tt);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}
}

MetaStore::MetaStore(MySQLPool& pool) : pool_(pool) {}

std::string MetaStore::Escape(const std::string& str) {
    // 使用连接池执行转义
    auto conn = pool_.GetConnection();
    if (!conn) {
        return str;
    }
    return conn->Escape(str);
}

std::chrono::system_clock::time_point MetaStore::ParseTimestamp(const char* str) {
    return ParseMysqlTime(str);
}

std::string MetaStore::GenerateVersionId() {
    // 生成对象版本 ID
    return UUID::Generate();
}

// ===== Bucket =====

Result<int64_t> MetaStore::CreateBucket(const std::string& name,
                                        const std::string& owner_id,
                                        const std::string& region,
                                        const std::string& acl) {
    // 插入 bucket 记录
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<int64_t>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "INSERT INTO buckets(name) VALUES('" + conn->Escape(name) + "')";
    if (!conn->Execute(sql)) {
        std::string err = conn->LastError();
        if (err.find("Duplicate") != std::string::npos) {
            return Result<int64_t>::Err(ErrorCode::BucketAlreadyExists, err);
        }
        return Result<int64_t>::Err(ErrorCode::DatabaseError, err);
    }
    return Result<int64_t>::Ok(static_cast<int64_t>(conn->LastInsertId()));
}

Result<BucketInfo> MetaStore::GetBucket(const std::string& name) {
    // 按名称查询 bucket
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<BucketInfo>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "SELECT id, name, created_at FROM buckets WHERE name='" + conn->Escape(name) + "' LIMIT 1";
    MYSQL_RES* res = conn->Query(sql);
    if (!res) {
        return Result<BucketInfo>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        return Result<BucketInfo>::Err(ErrorCode::NoSuchBucket, "Bucket not found");
    }
    BucketInfo info{};
    info.id = std::stoll(row[0]);
    info.name = row[1] ? row[1] : "";
    info.owner_id = "";
    info.region = "default";
    info.acl = "private";
    info.versioning_enabled = false;
    info.created_at = ParseTimestamp(row[2]);
    info.updated_at = info.created_at;
    mysql_free_result(res);
    return Result<BucketInfo>::Ok(info);
}

Result<BucketInfo> MetaStore::GetBucketById(int64_t id) {
    // 按 ID 查询 bucket
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<BucketInfo>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "SELECT id, name, created_at FROM buckets WHERE id=" + std::to_string(id) + " LIMIT 1";
    MYSQL_RES* res = conn->Query(sql);
    if (!res) {
        return Result<BucketInfo>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        return Result<BucketInfo>::Err(ErrorCode::NoSuchBucket, "Bucket not found");
    }
    BucketInfo info{};
    info.id = std::stoll(row[0]);
    info.name = row[1] ? row[1] : "";
    info.owner_id = "";
    info.region = "default";
    info.acl = "private";
    info.versioning_enabled = false;
    info.created_at = ParseTimestamp(row[2]);
    info.updated_at = info.created_at;
    mysql_free_result(res);
    return Result<BucketInfo>::Ok(info);
}

Result<std::vector<BucketInfo>> MetaStore::ListBuckets(const std::string& owner_id) {
    // 列出所有 bucket
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<std::vector<BucketInfo>>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "SELECT id, name, created_at FROM buckets ORDER BY name";
    MYSQL_RES* res = conn->Query(sql);
    if (!res) {
        return Result<std::vector<BucketInfo>>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    std::vector<BucketInfo> buckets;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        BucketInfo info{};
        info.id = std::stoll(row[0]);
        info.name = row[1] ? row[1] : "";
        info.owner_id = "";
        info.region = "default";
        info.acl = "private";
        info.versioning_enabled = false;
        info.created_at = ParseTimestamp(row[2]);
        info.updated_at = info.created_at;
        buckets.push_back(std::move(info));
    }
    mysql_free_result(res);
    return Result<std::vector<BucketInfo>>::Ok(std::move(buckets));
}

Status MetaStore::DeleteBucket(const std::string& name) {
    // 删除 bucket
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Status::Error(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "DELETE FROM buckets WHERE name='" + conn->Escape(name) + "'";
    if (!conn->Execute(sql)) {
        return Status::Error(ErrorCode::DatabaseError, conn->LastError());
    }
    if (conn->AffectedRows() == 0) {
        return Status::Error(ErrorCode::NoSuchBucket, "Bucket not found");
    }
    return Status::OK();
}

Result<bool> MetaStore::IsBucketEmpty(int64_t bucket_id) {
    // 判断 bucket 是否为空
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<bool>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "SELECT COUNT(*) FROM objects WHERE bucket_id=" + std::to_string(bucket_id);
    MYSQL_RES* res = conn->Query(sql);
    if (!res) {
        return Result<bool>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    int64_t count = row && row[0] ? std::stoll(row[0]) : 0;
    mysql_free_result(res);
    return Result<bool>::Ok(count == 0);
}

// ===== Object =====

Result<int64_t> MetaStore::PutObject(int64_t bucket_id,
                                     const std::string& key,
                                     int64_t size,
                                     const std::string& etag,
                                     const std::string& content_type,
                                     const std::string& sha256_hash,
                                     const std::string& owner_id,
                                     const std::string& metadata_json) {
    // 写入/更新对象元数据
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<int64_t>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string key_esc = conn->Escape(key);
    std::string sql = "INSERT INTO objects(bucket_id, object_key, cas_key, size, content_type, etag) VALUES(" +
        std::to_string(bucket_id) + ", '" + key_esc + "', '" + conn->Escape(sha256_hash) + "', " +
        std::to_string(size) + ", '" + conn->Escape(content_type) + "', '" + conn->Escape(etag) + "') "
        "ON DUPLICATE KEY UPDATE cas_key=VALUES(cas_key), size=VALUES(size), content_type=VALUES(content_type), etag=VALUES(etag)";
    if (!conn->Execute(sql)) {
        return Result<int64_t>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    if (conn->LastInsertId() != 0) {
        return Result<int64_t>::Ok(static_cast<int64_t>(conn->LastInsertId()));
    }
    // 查询现有 id
    std::string q = "SELECT id FROM objects WHERE bucket_id=" + std::to_string(bucket_id) + " AND object_key='" + key_esc + "' LIMIT 1";
    MYSQL_RES* res = conn->Query(q);
    if (!res) {
        return Result<int64_t>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        return Result<int64_t>::Err(ErrorCode::DatabaseError, "Failed to fetch object id");
    }
    int64_t id = std::stoll(row[0]);
    mysql_free_result(res);
    return Result<int64_t>::Ok(id);
}

Result<std::optional<std::string>> MetaStore::PutObjectWithRefCount(
    int64_t bucket_id,
    const std::string& key,
    int64_t size,
    const std::string& etag,
    const std::string& content_type,
    const std::string& sha256_hash) {
    // 事务性写入对象并维护 CAS ref_count
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }

    Transaction tx(conn);

    std::string key_esc = conn->Escape(key);
    std::string cas_esc = conn->Escape(sha256_hash);
    std::string etag_esc = conn->Escape(etag);
    std::string ct_esc = conn->Escape(content_type);

    // 读取旧 cas_key（加锁）
    std::string old_cas;
    {
        std::string sql = "SELECT cas_key FROM objects WHERE bucket_id=" + std::to_string(bucket_id) +
                          " AND object_key='" + key_esc + "' FOR UPDATE";
        MYSQL_RES* res = tx.Query(sql);
        if (!res) {
            return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
        }
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
            old_cas = row[0];
        }
        mysql_free_result(res);
    }

    bool should_inc_new = old_cas.empty() || old_cas != sha256_hash;

    // 新 cas_key 引用 +1
    if (should_inc_new) {
        std::string sql = "INSERT INTO cas_blobs(cas_key, size, ref_count) VALUES('" + cas_esc +
                          "', " + std::to_string(size) + ", 1) "
                          "ON DUPLICATE KEY UPDATE ref_count = ref_count + 1, updated_at=CURRENT_TIMESTAMP";
        if (!tx.Execute(sql)) {
            return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
        }
    }

    // 旧 cas_key 引用 -1，若为 0 则返回给 GC
    std::optional<std::string> gc_candidate;
    if (!old_cas.empty() && old_cas != sha256_hash) {
        std::string old_esc = conn->Escape(old_cas);
        std::string dec_sql = "UPDATE cas_blobs SET ref_count = IF(ref_count>0, ref_count-1, 0), "
                              "updated_at=CURRENT_TIMESTAMP WHERE cas_key='" + old_esc + "'";
        if (!tx.Execute(dec_sql)) {
            return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
        }

        std::string sel_sql = "SELECT ref_count FROM cas_blobs WHERE cas_key='" + old_esc + "' LIMIT 1";
        MYSQL_RES* res = tx.Query(sel_sql);
        if (!res) {
            return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
        }
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0] && std::stoi(row[0]) == 0) {
            gc_candidate = old_cas;
        }
        mysql_free_result(res);
    }

    // 写入/更新对象元数据
    std::string upsert = "INSERT INTO objects(bucket_id, object_key, cas_key, size, content_type, etag) VALUES(" +
                         std::to_string(bucket_id) + ", '" + key_esc + "', '" + cas_esc + "', " +
                         std::to_string(size) + ", '" + ct_esc + "', '" + etag_esc + "') "
                         "ON DUPLICATE KEY UPDATE cas_key=VALUES(cas_key), size=VALUES(size), "
                         "content_type=VALUES(content_type), etag=VALUES(etag)";
    if (!tx.Execute(upsert)) {
        return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
    }

    // 提交事务
    if (!tx.Commit()) {
        return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, "Failed to commit transaction");
    }

    return Result<std::optional<std::string>>::Ok(gc_candidate);
}

Result<ObjectInfo> MetaStore::GetObject(int64_t bucket_id, const std::string& key) {
    // 查询对象元数据
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<ObjectInfo>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "SELECT id, bucket_id, object_key, cas_key, size, etag, content_type, created_at FROM objects "
        "WHERE bucket_id=" + std::to_string(bucket_id) + " AND object_key='" + conn->Escape(key) + "' LIMIT 1";
    MYSQL_RES* res = conn->Query(sql);
    if (!res) {
        return Result<ObjectInfo>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        return Result<ObjectInfo>::Err(ErrorCode::NoSuchKey, "Object not found");
    }
    ObjectInfo info{};
    info.id = std::stoll(row[0]);
    info.bucket_id = std::stoll(row[1]);
    info.key = row[2] ? row[2] : "";
    info.sha256_hash = row[3] ? row[3] : "";
    info.size = row[4] ? std::stoll(row[4]) : 0;
    info.etag = row[5] ? row[5] : "";
    info.content_type = row[6] ? row[6] : "application/octet-stream";
    info.created_at = ParseTimestamp(row[7]);
    info.version_id = "";
    info.is_latest = true;
    info.is_delete_marker = false;
    info.storage_class = "standard";
    info.metadata_json = "{}";
    info.owner_id = "";
    mysql_free_result(res);
    return Result<ObjectInfo>::Ok(info);
}

Result<ObjectInfo> MetaStore::GetObjectVersion(int64_t bucket_id,
                                               const std::string& key,
                                               const std::string& version_id) {
    // 当前实现不支持多版本，直接返回最新版本
    // 当前实现不支持多版本，直接返回最新版本
    return GetObject(bucket_id, key);
}

Result<ListObjectsResult> MetaStore::ListObjects(int64_t bucket_id,
                                                 const std::string& prefix,
                                                 const std::string& delimiter,
                                                 const std::string& start_after,
                                                 int32_t max_keys) {
    // 列出对象（支持 prefix/start_after）
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<ListObjectsResult>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "SELECT id, bucket_id, object_key, cas_key, size, etag, content_type, created_at FROM objects WHERE bucket_id=" +
        std::to_string(bucket_id);
    if (!prefix.empty()) {
        sql += " AND object_key LIKE '" + conn->Escape(prefix) + "%'";
    }
    if (!start_after.empty()) {
        sql += " AND object_key > '" + conn->Escape(start_after) + "'";
    }
    sql += " ORDER BY object_key LIMIT " + std::to_string(max_keys + 1);

    MYSQL_RES* res = conn->Query(sql);
    if (!res) {
        return Result<ListObjectsResult>::Err(ErrorCode::DatabaseError, conn->LastError());
    }

    ListObjectsResult result{};
    MYSQL_ROW row;
    int32_t count = 0;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        if (count >= max_keys) {
            result.is_truncated = true;
            result.next_key_marker = row[2] ? row[2] : "";
            result.next_continuation_token = result.next_key_marker;
            break;
        }
        ObjectInfo info{};
        info.id = std::stoll(row[0]);
        info.bucket_id = std::stoll(row[1]);
        info.key = row[2] ? row[2] : "";
        info.sha256_hash = row[3] ? row[3] : "";
        info.size = row[4] ? std::stoll(row[4]) : 0;
        info.etag = row[5] ? row[5] : "";
        info.content_type = row[6] ? row[6] : "application/octet-stream";
        info.created_at = ParseTimestamp(row[7]);
        info.version_id = "";
        info.is_latest = true;
        info.is_delete_marker = false;
        info.storage_class = "standard";
        info.metadata_json = "{}";
        info.owner_id = "";
        result.objects.push_back(std::move(info));
        ++count;
    }
    mysql_free_result(res);
    return Result<ListObjectsResult>::Ok(result);
}

Status MetaStore::DeleteObject(int64_t bucket_id, const std::string& key) {
    // 删除对象记录
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Status::Error(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "DELETE FROM objects WHERE bucket_id=" + std::to_string(bucket_id) +
        " AND object_key='" + conn->Escape(key) + "'";
    if (!conn->Execute(sql)) {
        return Status::Error(ErrorCode::DatabaseError, conn->LastError());
    }
    if (conn->AffectedRows() == 0) {
        return Status::Error(ErrorCode::NoSuchKey, "Object not found");
    }
    return Status::OK();
}

Result<std::optional<std::string>> MetaStore::DeleteObjectWithRefCount(int64_t bucket_id,
                                                                       const std::string& key) {
    // 事务性删除对象并维护 CAS ref_count
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }

    Transaction tx(conn);
    std::string key_esc = conn->Escape(key);
    // 获取 cas_key（加锁）
    std::string cas_key;
    {
        std::string sql = "SELECT cas_key FROM objects WHERE bucket_id=" + std::to_string(bucket_id) +
                          " AND object_key='" + key_esc + "' FOR UPDATE";
        MYSQL_RES* res = tx.Query(sql);
        if (!res) {
            return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
        }
        MYSQL_ROW row = mysql_fetch_row(res);
        if (!row || !row[0]) {
            mysql_free_result(res);
            return Result<std::optional<std::string>>::Err(ErrorCode::NoSuchKey, "Object not found");
        }
        cas_key = row[0];
        mysql_free_result(res);
    }

    // 删除对象记录
    std::string del_sql = "DELETE FROM objects WHERE bucket_id=" + std::to_string(bucket_id) +
                          " AND object_key='" + key_esc + "'";
    if (!tx.Execute(del_sql)) {
        return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
    }

    // CAS 引用 -1，若为 0 返回给 GC
    std::optional<std::string> gc_candidate;
    if (!cas_key.empty()) {
        std::string cas_esc = conn->Escape(cas_key);
        std::string dec_sql = "UPDATE cas_blobs SET ref_count = IF(ref_count>0, ref_count-1, 0), "
                              "updated_at=CURRENT_TIMESTAMP WHERE cas_key='" + cas_esc + "'";
        if (!tx.Execute(dec_sql)) {
            return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
        }
        std::string sel_sql = "SELECT ref_count FROM cas_blobs WHERE cas_key='" + cas_esc + "' LIMIT 1";
        MYSQL_RES* res = tx.Query(sel_sql);
        if (!res) {
            return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
        }
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0] && std::stoi(row[0]) == 0) {
            gc_candidate = cas_key;
        }
        mysql_free_result(res);
    }

    // 提交事务
    if (!tx.Commit()) {
        return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, "Failed to commit transaction");
    }

    return Result<std::optional<std::string>>::Ok(gc_candidate);
}

Status MetaStore::DeleteObjectVersion(int64_t bucket_id,
                                      const std::string& key,
                                      const std::string& version_id) {
    // 版本化未实现，直接删除最新
    return DeleteObject(bucket_id, key);
}

// ===== CAS =====

Result<int64_t> MetaStore::RegisterCasBlob(const std::string& sha256_hash, int64_t size) {
    // 注册/引用计数 +1
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<int64_t>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "INSERT INTO cas_blobs(cas_key, size, ref_count) VALUES('" + conn->Escape(sha256_hash) +
        "', " + std::to_string(size) + ", 1) ON DUPLICATE KEY UPDATE ref_count = ref_count + 1, updated_at=CURRENT_TIMESTAMP";
    if (!conn->Execute(sql)) {
        return Result<int64_t>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    return Result<int64_t>::Ok(1);
}

Result<CasBlobInfo> MetaStore::GetCasBlob(const std::string& sha256_hash) {
    // 查询 CAS blob 信息
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<CasBlobInfo>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "SELECT cas_key, size, ref_count, created_at, updated_at FROM cas_blobs WHERE cas_key='" +
        conn->Escape(sha256_hash) + "' LIMIT 1";
    MYSQL_RES* res = conn->Query(sql);
    if (!res) {
        return Result<CasBlobInfo>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        return Result<CasBlobInfo>::Err(ErrorCode::NoSuchKey, "CAS blob not found");
    }
    CasBlobInfo info{};
    info.id = 0;
    info.sha256_hash = row[0] ? row[0] : "";
    info.size = row[1] ? std::stoll(row[1]) : 0;
    info.ref_count = row[2] ? std::stoi(row[2]) : 0;
    info.created_at = ParseTimestamp(row[3]);
    info.last_accessed = ParseTimestamp(row[4]);
    mysql_free_result(res);
    return Result<CasBlobInfo>::Ok(info);
}

Status MetaStore::IncrementRefCount(const std::string& sha256_hash) {
    // CAS 引用计数 +1
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Status::Error(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "UPDATE cas_blobs SET ref_count = ref_count + 1, updated_at=CURRENT_TIMESTAMP WHERE cas_key='" +
        conn->Escape(sha256_hash) + "'";
    if (!conn->Execute(sql)) {
        return Status::Error(ErrorCode::DatabaseError, conn->LastError());
    }
    if (conn->AffectedRows() == 0) {
        return Status::Error(ErrorCode::NoSuchKey, "CAS blob not found");
    }
    return Status::OK();
}

Status MetaStore::DecrementRefCount(const std::string& sha256_hash) {
    // CAS 引用计数 -1（不低于 0）
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Status::Error(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "UPDATE cas_blobs SET ref_count = IF(ref_count>0, ref_count-1, 0), updated_at=CURRENT_TIMESTAMP WHERE cas_key='" +
        conn->Escape(sha256_hash) + "'";
    if (!conn->Execute(sql)) {
        return Status::Error(ErrorCode::DatabaseError, conn->LastError());
    }
    if (conn->AffectedRows() == 0) {
        return Status::Error(ErrorCode::NoSuchKey, "CAS blob not found");
    }
    return Status::OK();
}

Result<bool> MetaStore::DecrementRefCountAndCheckZero(const std::string& sha256_hash) {
    // CAS 引用计数 -1 并检查是否为 0
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<bool>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string hash_esc = conn->Escape(sha256_hash);
    std::string dec_sql = "UPDATE cas_blobs SET ref_count = IF(ref_count>0, ref_count-1, 0), "
                          "updated_at=CURRENT_TIMESTAMP WHERE cas_key='" + hash_esc + "'";
    if (!conn->Execute(dec_sql)) {
        return Result<bool>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    std::string sel_sql = "SELECT ref_count FROM cas_blobs WHERE cas_key='" + hash_esc + "' LIMIT 1";
    MYSQL_RES* res = conn->Query(sel_sql);
    if (!res) {
        return Result<bool>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row || !row[0]) {
        mysql_free_result(res);
        return Result<bool>::Err(ErrorCode::NoSuchKey, "CAS blob not found");
    }
    bool zero = (std::stoi(row[0]) == 0);
    mysql_free_result(res);
    return Result<bool>::Ok(zero);
}

Status MetaStore::DecreaseCasBlobRefCount(const std::string& cas_key) {
    return DecrementRefCount(cas_key);
}

Result<std::vector<CasBlobInfo>> MetaStore::GetGarbageBlobs(int32_t min_age_seconds,
                                                            int32_t limit) {
    // 查询可回收的 CAS blobs
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<std::vector<CasBlobInfo>>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "SELECT cas_key, size, ref_count, created_at, updated_at FROM cas_blobs "
        "WHERE ref_count = 0 AND updated_at < (NOW() - INTERVAL " + std::to_string(min_age_seconds) + " SECOND) "
        "ORDER BY updated_at LIMIT " + std::to_string(limit);
    MYSQL_RES* res = conn->Query(sql);
    if (!res) {
        return Result<std::vector<CasBlobInfo>>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    std::vector<CasBlobInfo> blobs;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        CasBlobInfo info{};
        info.id = 0;
        info.sha256_hash = row[0] ? row[0] : "";
        info.size = row[1] ? std::stoll(row[1]) : 0;
        info.ref_count = row[2] ? std::stoi(row[2]) : 0;
        info.created_at = ParseTimestamp(row[3]);
        info.last_accessed = ParseTimestamp(row[4]);
        blobs.push_back(std::move(info));
    }
    mysql_free_result(res);
    return Result<std::vector<CasBlobInfo>>::Ok(std::move(blobs));
}

Status MetaStore::DeleteCasBlob(const std::string& sha256_hash) {
    // 删除 CAS blob 元数据
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Status::Error(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "DELETE FROM cas_blobs WHERE cas_key='" + conn->Escape(sha256_hash) + "'";
    if (!conn->Execute(sql)) {
        return Status::Error(ErrorCode::DatabaseError, conn->LastError());
    }
    return Status::OK();
}

// ===== Multipart =====

Result<std::string> MetaStore::CreateMultipartUpload(const std::string& upload_id,
                                                     int64_t bucket_id,
                                                     const std::string& key,
                                                     const std::string& content_type,
                                                     const std::string& owner_id,
                                                     std::chrono::system_clock::time_point expires_at) {
    // 创建 multipart 上传记录
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<std::string>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "INSERT INTO multipart_uploads(upload_id, bucket_id, object_key, content_type, expires_at) VALUES('" +
        conn->Escape(upload_id) + "', " + std::to_string(bucket_id) + ", '" + conn->Escape(key) + "', '" +
        conn->Escape(content_type) + "', '" + conn->Escape(TimePointToMySQL(expires_at)) + "')";
    if (!conn->Execute(sql)) {
        return Result<std::string>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    return Result<std::string>::Ok(upload_id);
}

Result<MultipartUploadInfo> MetaStore::GetMultipartUpload(const std::string& upload_id) {
    // 查询 multipart 上传记录
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<MultipartUploadInfo>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "SELECT upload_id, bucket_id, object_key, content_type, created_at FROM multipart_uploads WHERE upload_id='" +
        conn->Escape(upload_id) + "' LIMIT 1";
    MYSQL_RES* res = conn->Query(sql);
    if (!res) {
        return Result<MultipartUploadInfo>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        return Result<MultipartUploadInfo>::Err(ErrorCode::NoSuchUpload, "Upload not found");
    }
    MultipartUploadInfo info{};
    info.id = 0;
    info.upload_id = row[0] ? row[0] : "";
    info.bucket_id = row[1] ? std::stoll(row[1]) : 0;
    info.key = row[2] ? row[2] : "";
    info.content_type = row[3] ? row[3] : "application/octet-stream";
    info.created_at = ParseTimestamp(row[4]);
    info.metadata_json = "{}";
    info.owner_id = "";
    mysql_free_result(res);
    return Result<MultipartUploadInfo>::Ok(info);
}

Status MetaStore::DeleteMultipartUpload(const std::string& upload_id) {
    // 删除 multipart 上传记录
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Status::Error(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "DELETE FROM multipart_uploads WHERE upload_id='" + conn->Escape(upload_id) + "'";
    if (!conn->Execute(sql)) {
        return Status::Error(ErrorCode::DatabaseError, conn->LastError());
    }
    if (conn->AffectedRows() == 0) {
        return Status::Error(ErrorCode::NoSuchUpload, "Upload not found");
    }
    return Status::OK();
}

Result<std::vector<MultipartUploadInfo>> MetaStore::ListMultipartUploads(
    int64_t bucket_id,
    const std::string& prefix,
    int32_t max_uploads) {
    // 列出 multipart 上传记录
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<std::vector<MultipartUploadInfo>>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "SELECT upload_id, bucket_id, object_key, content_type, created_at FROM multipart_uploads WHERE bucket_id=" +
        std::to_string(bucket_id);
    if (!prefix.empty()) {
        sql += " AND object_key LIKE '" + conn->Escape(prefix) + "%'";
    }
    sql += " ORDER BY created_at LIMIT " + std::to_string(max_uploads);

    MYSQL_RES* res = conn->Query(sql);
    if (!res) {
        return Result<std::vector<MultipartUploadInfo>>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    std::vector<MultipartUploadInfo> uploads;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        MultipartUploadInfo info{};
        info.id = 0;
        info.upload_id = row[0] ? row[0] : "";
        info.bucket_id = row[1] ? std::stoll(row[1]) : 0;
        info.key = row[2] ? row[2] : "";
        info.content_type = row[3] ? row[3] : "application/octet-stream";
        info.created_at = ParseTimestamp(row[4]);
        info.metadata_json = "{}";
        info.owner_id = "";
        uploads.push_back(std::move(info));
    }
    mysql_free_result(res);
    return Result<std::vector<MultipartUploadInfo>>::Ok(std::move(uploads));
}

Result<int64_t> MetaStore::CreateOrUpdatePart(const std::string& upload_id,
                                              int32_t part_number,
                                              const std::string& sha256_hash,
                                              int64_t size,
                                              const std::string& etag) {
    // 创建或更新分片记录
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<int64_t>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "INSERT INTO multipart_parts(upload_id, part_number, cas_key, size, etag) VALUES('" +
        conn->Escape(upload_id) + "', " + std::to_string(part_number) + ", '" + conn->Escape(sha256_hash) + "', " +
        std::to_string(size) + ", '" + conn->Escape(etag) + "') "
        "ON DUPLICATE KEY UPDATE cas_key=VALUES(cas_key), size=VALUES(size), etag=VALUES(etag)";
    if (!conn->Execute(sql)) {
        return Result<int64_t>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    return Result<int64_t>::Ok(1);
}

Result<std::optional<std::string>> MetaStore::PutPartWithRefCount(const std::string& upload_id,
                                                                  int32_t part_number,
                                                                  const std::string& sha256_hash,
                                                                  int64_t size,
                                                                  const std::string& etag) {
    // 事务性写入分片并维护 CAS ref_count
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }

    Transaction tx(conn);
    std::string upload_esc = conn->Escape(upload_id);
    std::string cas_esc = conn->Escape(sha256_hash);
    std::string etag_esc = conn->Escape(etag);

    // 查询旧 cas_key（加锁）
    std::string old_cas;
    {
        std::string sql = "SELECT cas_key FROM multipart_parts WHERE upload_id='" + upload_esc +
                          "' AND part_number=" + std::to_string(part_number) + " FOR UPDATE";
        MYSQL_RES* res = tx.Query(sql);
        if (!res) {
            return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
        }
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
            old_cas = row[0];
        }
        mysql_free_result(res);
    }

    // 新 cas_key 引用 +1
    bool should_inc_new = old_cas.empty() || old_cas != sha256_hash;
    if (should_inc_new) {
        std::string sql = "INSERT INTO cas_blobs(cas_key, size, ref_count) VALUES('" + cas_esc +
                          "', " + std::to_string(size) + ", 1) "
                          "ON DUPLICATE KEY UPDATE ref_count = ref_count + 1, updated_at=CURRENT_TIMESTAMP";
        if (!tx.Execute(sql)) {
            return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
        }
    }

    // 旧 cas_key 引用 -1，若为 0 返回给 GC
    std::optional<std::string> gc_candidate;
    if (!old_cas.empty() && old_cas != sha256_hash) {
        std::string old_esc = conn->Escape(old_cas);
        std::string dec_sql = "UPDATE cas_blobs SET ref_count = IF(ref_count>0, ref_count-1, 0), "
                              "updated_at=CURRENT_TIMESTAMP WHERE cas_key='" + old_esc + "'";
        if (!tx.Execute(dec_sql)) {
            return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
        }
        std::string sel_sql = "SELECT ref_count FROM cas_blobs WHERE cas_key='" + old_esc + "' LIMIT 1";
        MYSQL_RES* res = tx.Query(sel_sql);
        if (!res) {
            return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
        }
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0] && std::stoi(row[0]) == 0) {
            gc_candidate = old_cas;
        }
        mysql_free_result(res);
    }

    // 写入/更新分片记录
    std::string upsert = "INSERT INTO multipart_parts(upload_id, part_number, cas_key, size, etag) VALUES('" +
                         upload_esc + "', " + std::to_string(part_number) + ", '" + cas_esc + "', " +
                         std::to_string(size) + ", '" + etag_esc + "') "
                         "ON DUPLICATE KEY UPDATE cas_key=VALUES(cas_key), size=VALUES(size), etag=VALUES(etag)";
    if (!tx.Execute(upsert)) {
        return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, conn->LastError());
    }

    // 提交事务
    if (!tx.Commit()) {
        return Result<std::optional<std::string>>::Err(ErrorCode::DatabaseError, "Failed to commit transaction");
    }

    return Result<std::optional<std::string>>::Ok(gc_candidate);
}

Result<int64_t> MetaStore::PutPart(const std::string& upload_id,
                                   int32_t part_number,
                                   const std::string& sha256_hash,
                                   int64_t size,
                                   const std::string& etag) {
    // 便捷方法：复用 CreateOrUpdatePart
    return CreateOrUpdatePart(upload_id, part_number, sha256_hash, size, etag);
}

Result<ListPartsResult> MetaStore::ListParts(const std::string& upload_id,
                                             int32_t part_number_marker,
                                             int32_t max_parts) {
    // 列出分片（支持 marker 与分页）
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<ListPartsResult>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "SELECT upload_id, part_number, cas_key, size, etag, created_at FROM multipart_parts WHERE upload_id='" +
        conn->Escape(upload_id) + "' AND part_number > " + std::to_string(part_number_marker) +
        " ORDER BY part_number LIMIT " + std::to_string(max_parts + 1);
    MYSQL_RES* res = conn->Query(sql);
    if (!res) {
        return Result<ListPartsResult>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    ListPartsResult result{};
    MYSQL_ROW row;
    int32_t count = 0;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        if (count >= max_parts) {
            result.is_truncated = true;
            result.next_part_number_marker = row[1] ? std::stoi(row[1]) : 0;
            break;
        }
        MultipartPartInfo info{};
        info.id = 0;
        info.upload_id = row[0] ? row[0] : "";
        info.part_number = row[1] ? std::stoi(row[1]) : 0;
        info.sha256_hash = row[2] ? row[2] : "";
        info.size = row[3] ? std::stoll(row[3]) : 0;
        info.etag = row[4] ? row[4] : "";
        info.created_at = ParseTimestamp(row[5]);
        result.parts.push_back(std::move(info));
        ++count;
    }
    mysql_free_result(res);
    return Result<ListPartsResult>::Ok(result);
}

Status MetaStore::CompleteMultipartUpload(const std::string& upload_id) {
    // 完成上传后删除记录（简化处理）
    return DeleteMultipartUpload(upload_id);
}

Status MetaStore::AbortMultipartUpload(const std::string& upload_id) {
    // 取消上传后删除记录（简化处理）
    return DeleteMultipartUpload(upload_id);
}

// ===== Idempotency =====

Result<std::optional<IdempotencyRecord>> MetaStore::CheckIdempotency(
    const std::string& idempotency_key) {
    // 查询幂等性记录（若过期则视为不存在）
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Result<std::optional<IdempotencyRecord>>::Err(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "SELECT idempotency_key, request_hash, response_status, response_body, created_at, expires_at "
        "FROM idempotency_records WHERE idempotency_key='" + conn->Escape(idempotency_key) + "' LIMIT 1";
    MYSQL_RES* res = conn->Query(sql);
    if (!res) {
        return Result<std::optional<IdempotencyRecord>>::Err(ErrorCode::DatabaseError, conn->LastError());
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        mysql_free_result(res);
        return Result<std::optional<IdempotencyRecord>>::Ok(std::nullopt);
    }
    IdempotencyRecord record{};
    record.idempotency_key = row[0] ? row[0] : "";
    record.request_hash = row[1] ? row[1] : "";
    record.response_status = row[2] ? std::stoi(row[2]) : 0;
    record.response_body = row[3] ? row[3] : "";
    record.created_at = ParseTimestamp(row[4]);
    record.expires_at = ParseTimestamp(row[5]);
    mysql_free_result(res);

    auto now = std::chrono::system_clock::now();
    if (record.expires_at <= now) {
        return Result<std::optional<IdempotencyRecord>>::Ok(std::nullopt);
    }
    return Result<std::optional<IdempotencyRecord>>::Ok(record);
}

Status MetaStore::RecordIdempotency(const std::string& idempotency_key,
                                    const std::string& request_hash,
                                    int response_status,
                                    const std::string& response_body,
                                    int32_t ttl_seconds) {
    // 写入/更新幂等性记录
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Status::Error(ErrorCode::DatabaseError, "No MySQL connection");
    }
    auto expires_at = std::chrono::system_clock::now() + std::chrono::seconds(ttl_seconds);
    std::string sql = "INSERT INTO idempotency_records(idempotency_key, request_hash, response_status, response_body, expires_at) VALUES('" +
        conn->Escape(idempotency_key) + "', '" + conn->Escape(request_hash) + "', " +
        std::to_string(response_status) + ", '" + conn->Escape(response_body) + "', '" +
        conn->Escape(TimePointToMySQL(expires_at)) + "') "
        "ON DUPLICATE KEY UPDATE request_hash=VALUES(request_hash), response_status=VALUES(response_status), response_body=VALUES(response_body), expires_at=VALUES(expires_at)";
    if (!conn->Execute(sql)) {
        return Status::Error(ErrorCode::DatabaseError, conn->LastError());
    }
    return Status::OK();
}

Status MetaStore::CleanupIdempotencyRecords() {
    // 清理过期幂等性记录
    auto conn = pool_.GetConnection();
    if (!conn) {
        return Status::Error(ErrorCode::DatabaseError, "No MySQL connection");
    }
    std::string sql = "DELETE FROM idempotency_records WHERE expires_at < NOW()";
    if (!conn->Execute(sql)) {
        return Status::Error(ErrorCode::DatabaseError, conn->LastError());
    }
    return Status::OK();
}

} // namespace minis3
