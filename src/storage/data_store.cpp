#include "data_store.h"
#include "util/fs.h"
#include "util/uuid.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>
#include <spdlog/spdlog.h>

namespace minis3 {

// WriteSession 实现

WriteSession::WriteSession(const std::string& tmp_path, int fd)
    : tmp_path_(tmp_path)
    , fd_(fd)
    , bytes_written_(0)
    , finished_(false) {
}

WriteSession::~WriteSession() {
    // 未完成写入则回滚并删除临时文件
    if (!finished_ && fd_ >= 0) {
        Abort();
    }
}

WriteSession::WriteSession(WriteSession&& other) noexcept
    : tmp_path_(std::move(other.tmp_path_))
    , fd_(other.fd_)
    , bytes_written_(other.bytes_written_)
    , sha256_ctx_(std::move(other.sha256_ctx_))
    , finished_(other.finished_) {
    // 移动后使对方无效，避免重复关闭
    other.fd_ = -1;
    other.finished_ = true;
}

WriteSession& WriteSession::operator=(WriteSession&& other) noexcept {
    if (this != &other) {
        // 覆盖前先回滚本会话
        if (!finished_ && fd_ >= 0) {
            Abort();
        }
        
        tmp_path_ = std::move(other.tmp_path_);
        fd_ = other.fd_;
        bytes_written_ = other.bytes_written_;
        sha256_ctx_ = std::move(other.sha256_ctx_);
        finished_ = other.finished_;
        
        other.fd_ = -1;
        other.finished_ = true;
    }
    return *this;
}

Status WriteSession::Write(const char* data, size_t len) {
    // 已完成写入不可再写
    if (finished_) {
        return Status::Error(ErrorCode::InternalError, "WriteSession already finished");
    }
    
    // 循环写入，处理 EINTR
    size_t written = 0;
    while (written < len) {
        ssize_t n = ::write(fd_, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return Status::Error(ErrorCode::StorageError, 
                "Write failed: " + std::string(strerror(errno)));
        }
        written += n;
    }
    
    // 更新哈希与计数
    sha256_ctx_.Update(reinterpret_cast<const uint8_t*>(data), len);
    bytes_written_ += len;
    
    return Status::OK();
}

Result<std::string> WriteSession::Finish() {
    // 完成写入并返回 SHA256
    if (finished_) {
        return Status::Error(ErrorCode::InternalError, "WriteSession already finished");
    }
    
    finished_ = true;
    
    // 确保数据落盘
    if (::fsync(fd_) != 0) {
        spdlog::warn("fsync failed: {}", strerror(errno));
    }
    
    ::close(fd_);
    fd_ = -1;
    
    return sha256_ctx_.Final();
}

void WriteSession::Abort() {
    // 关闭 fd 并删除临时文件
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    
    if (!tmp_path_.empty()) {
        ::unlink(tmp_path_.c_str());
        tmp_path_.clear();
    }
    
    finished_ = true;
}

// DataStore 实现

DataStore::DataStore(const std::string& data_dir, const std::string& tmp_dir)
    : layout_(data_dir)
    , tmp_dir_(tmp_dir) {
}

Status DataStore::Init() {
    // 创建必要的目录
    auto status = FileSystem::CreateDirectories(layout_.BaseDir());
    if (!status.ok()) {
        return status;
    }
    
    status = FileSystem::CreateDirectories(FileSystem::JoinPath(layout_.BaseDir(), "cas"));
    if (!status.ok()) {
        return status;
    }
    
    status = FileSystem::CreateDirectories(FileSystem::JoinPath(tmp_dir_, "upload"));
    if (!status.ok()) {
        return status;
    }
    
    status = FileSystem::CreateDirectories(FileSystem::JoinPath(tmp_dir_, "merge"));
    if (!status.ok()) {
        return status;
    }
    
    status = FileSystem::CreateDirectories(FileSystem::JoinPath(tmp_dir_, "multipart"));
    if (!status.ok()) {
        return status;
    }
    
    spdlog::info("DataStore initialized, data_dir: {}, tmp_dir: {}", 
        layout_.BaseDir(), tmp_dir_);
    
    return Status::OK();
}

Result<std::unique_ptr<WriteSession>> DataStore::BeginWrite() {
    // 创建唯一临时文件用于流式写入
    std::string tmp_path = FileSystem::JoinPath(
        FileSystem::JoinPath(tmp_dir_, "upload"), 
        UUID::Generate() + ".part");
    
    int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        return Status::Error(ErrorCode::StorageError, 
            "Failed to create temp file: " + std::string(strerror(errno)));
    }
    
    return std::make_unique<WriteSession>(tmp_path, fd);
}

Result<std::string> DataStore::CommitWrite(std::unique_ptr<WriteSession> session,
                                            const std::string& expected_sha256) {
    // 完成写入并得到 CAS key
    auto result = session->Finish();
    if (!result.ok()) {
        return result.status();
    }
    
    std::string cas_key = result.value();
    
    // 验证 SHA256（如果提供）
    if (!expected_sha256.empty() && cas_key != expected_sha256) {
        FileSystem::RemoveFile(session->TmpPath());
        return Status::Error(ErrorCode::ChecksumMismatch, 
            "SHA256 mismatch: expected " + expected_sha256 + ", got " + cas_key);
    }
    
    std::string final_path = layout_.GetCasPath(cas_key);
    
    // 检查是否已存在（去重）
    if (FileSystem::FileExists(final_path)) {
        // 删除临时文件，使用已存在的
        FileSystem::RemoveFile(session->TmpPath());
        spdlog::debug("CAS dedup: {} already exists", cas_key);
        return cas_key;
    }
    
    // 创建目标目录
    auto status = FileSystem::CreateDirectories(layout_.GetCasDir(cas_key));
    if (!status.ok()) {
        FileSystem::RemoveFile(session->TmpPath());
        return status;
    }
    
    // 原子重命名
    status = FileSystem::AtomicRename(session->TmpPath(), final_path);
    if (!status.ok()) {
        FileSystem::RemoveFile(session->TmpPath());
        return status;
    }
    
    spdlog::debug("CAS stored: {} ({} bytes)", cas_key, session->BytesWritten());
    
    return cas_key;
}

Result<std::string> DataStore::Write(const char* data, size_t len) {
    // 小文件：创建会话、写入并提交
    auto session_result = BeginWrite();
    if (!session_result.ok()) {
        return session_result.status();
    }
    
    auto session = std::move(session_result.value());
    
    auto status = session->Write(data, len);
    if (!status.ok()) {
        return status;
    }
    
    return CommitWrite(std::move(session));
}

Result<std::string> DataStore::Write(const std::string& data) {
    return Write(data.data(), data.size());
}

Result<std::string> DataStore::Read(const std::string& cas_key) {
    // 读取完整 CAS 文件到内存
    std::string path = layout_.GetCasPath(cas_key);
    
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return Status::Error(ErrorCode::NoSuchKey, 
            "CAS not found: " + cas_key);
    }
    
    struct stat st;
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        return Status::Error(ErrorCode::StorageError, 
            "Failed to stat file: " + std::string(strerror(errno)));
    }
    
    std::string data;
    data.resize(st.st_size);
    
    size_t total_read = 0;
    while (total_read < static_cast<size_t>(st.st_size)) {
        ssize_t n = ::read(fd, data.data() + total_read, st.st_size - total_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return Status::Error(ErrorCode::StorageError, 
                "Read failed: " + std::string(strerror(errno)));
        }
        if (n == 0) break;
        total_read += n;
    }
    
    ::close(fd);
    data.resize(total_read);
    
    return data;
}

Status DataStore::StreamRead(const std::string& cas_key, size_t offset, size_t length,
                              std::function<bool(const char* data, size_t len)> callback) {
    // 流式读取 CAS 文件
    std::string path = layout_.GetCasPath(cas_key);
    
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return Status::Error(ErrorCode::NoSuchKey, "CAS not found: " + cas_key);
    }
    
    if (offset > 0) {
        if (::lseek(fd, offset, SEEK_SET) == -1) {
            ::close(fd);
            return Status::Error(ErrorCode::StorageError, 
                "Seek failed: " + std::string(strerror(errno)));
        }
    }
    
    // 按块读取并回调
    char buffer[65536];  // 64KB buffer
    size_t remaining = length;
    
    while (length == 0 || remaining > 0) {
        size_t to_read = sizeof(buffer);
        if (length > 0 && remaining < to_read) {
            to_read = remaining;
        }
        
        ssize_t n = ::read(fd, buffer, to_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return Status::Error(ErrorCode::StorageError, 
                "Read failed: " + std::string(strerror(errno)));
        }
        if (n == 0) break;
        
        if (!callback(buffer, n)) {
            break;  // 回调请求停止
        }
        
        if (length > 0) {
            remaining -= n;
        }
    }
    
    ::close(fd);
    return Status::OK();
}

std::string DataStore::GetFilePath(const std::string& cas_key) const {
    return layout_.GetCasPath(cas_key);
}

bool DataStore::Exists(const std::string& cas_key) const {
    return FileSystem::FileExists(layout_.GetCasPath(cas_key));
}

Result<size_t> DataStore::GetSize(const std::string& cas_key) const {
    return FileSystem::GetFileSize(layout_.GetCasPath(cas_key));
}

Status DataStore::Delete(const std::string& cas_key) {
    std::string path = layout_.GetCasPath(cas_key);
    return FileSystem::RemoveFile(path);
}

Result<std::string> DataStore::Merge(const std::vector<std::string>& cas_keys) {
    // 合并多个 CAS 文件为一个新的 CAS
    if (cas_keys.empty()) {
        return Status::Error(ErrorCode::InvalidArgument, "No CAS keys to merge");
    }
    
    // 创建临时合并文件
    std::string tmp_path = FileSystem::JoinPath(
        FileSystem::JoinPath(tmp_dir_, "merge"), 
        UUID::Generate() + ".part");
    
    int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        return Status::Error(ErrorCode::StorageError, 
            "Failed to create merge file: " + std::string(strerror(errno)));
    }
    
    Crypto::SHA256Context sha256_ctx;
    size_t total_size = 0;
    
    // 依次读取每个 CAS 文件并写入合并文件
    char buffer[65536];
    
    for (const auto& cas_key : cas_keys) {
        std::string src_path = layout_.GetCasPath(cas_key);
        int src_fd = ::open(src_path.c_str(), O_RDONLY);
        if (src_fd < 0) {
            ::close(fd);
            ::unlink(tmp_path.c_str());
            return Status::Error(ErrorCode::NoSuchKey, "CAS not found: " + cas_key);
        }
        
        while (true) {
            ssize_t n = ::read(src_fd, buffer, sizeof(buffer));
            if (n < 0) {
                if (errno == EINTR) continue;
                ::close(src_fd);
                ::close(fd);
                ::unlink(tmp_path.c_str());
                return Status::Error(ErrorCode::StorageError, 
                    "Read failed: " + std::string(strerror(errno)));
            }
            if (n == 0) break;
            
            sha256_ctx.Update(reinterpret_cast<uint8_t*>(buffer), n);
            
            size_t written = 0;
            while (written < static_cast<size_t>(n)) {
                ssize_t w = ::write(fd, buffer + written, n - written);
                if (w < 0) {
                    if (errno == EINTR) continue;
                    ::close(src_fd);
                    ::close(fd);
                    ::unlink(tmp_path.c_str());
                    return Status::Error(ErrorCode::StorageError, 
                        "Write failed: " + std::string(strerror(errno)));
                }
                written += w;
            }
            
            total_size += n;
        }
        
        ::close(src_fd);
    }
    
    ::fsync(fd);
    ::close(fd);
    
    std::string cas_key = sha256_ctx.Final();
    std::string final_path = layout_.GetCasPath(cas_key);
    
    // 检查是否已存在（去重）
    if (FileSystem::FileExists(final_path)) {
        ::unlink(tmp_path.c_str());
        return cas_key;
    }
    
    // 创建目标目录并移动文件
    auto status = FileSystem::CreateDirectories(layout_.GetCasDir(cas_key));
    if (!status.ok()) {
        ::unlink(tmp_path.c_str());
        return status;
    }
    
    status = FileSystem::AtomicRename(tmp_path, final_path);
    if (!status.ok()) {
        ::unlink(tmp_path.c_str());
        return status;
    }
    
    spdlog::debug("CAS merged: {} ({} bytes, {} parts)", 
        cas_key, total_size, cas_keys.size());
    
    return cas_key;
}

} // namespace minis3
