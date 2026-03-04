#include "fs.h"
#include "uuid.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <cstring>

namespace minis3 {

Status FileSystem::CreateDirectories(const std::string& path) {
    // 创建目录（递归）
    try {
        std::filesystem::create_directories(path);
        return Status::OK();
    } catch (const std::exception& e) {
        return Status::Error(ErrorCode::StorageError, 
            "Failed to create directory: " + std::string(e.what()));
    }
}

Status FileSystem::RemoveFile(const std::string& path) {
    // 删除文件（不存在视为成功）
    if (::unlink(path.c_str()) != 0) {
        if (errno == ENOENT) {
            return Status::OK();  // 文件不存在视为成功
        }
        return Status::Error(ErrorCode::StorageError,
            "Failed to remove file: " + std::string(strerror(errno)));
    }
    return Status::OK();
}

Status FileSystem::RemoveDirectory(const std::string& path) {
    // 删除目录树
    try {
        std::filesystem::remove_all(path);
        return Status::OK();
    } catch (const std::exception& e) {
        return Status::Error(ErrorCode::StorageError,
            "Failed to remove directory: " + std::string(e.what()));
    }
}

Status FileSystem::AtomicRename(const std::string& src, const std::string& dst) {
    // 确保目标目录存在后原子重命名
    // 确保目标目录存在
    auto parent = std::filesystem::path(dst).parent_path();
    if (!parent.empty()) {
        auto status = CreateDirectories(parent.string());
        if (!status.ok()) {
            return status;
        }
    }
    
    if (::rename(src.c_str(), dst.c_str()) != 0) {
        return Status::Error(ErrorCode::StorageError,
            "Failed to rename file: " + std::string(strerror(errno)));
    }
    return Status::OK();
}

bool FileSystem::FileExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool FileSystem::DirectoryExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

Result<size_t> FileSystem::GetFileSize(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        return Status::Error(ErrorCode::StorageError,
            "Failed to get file size: " + std::string(strerror(errno)));
    }
    return static_cast<size_t>(st.st_size);
}

Result<size_t> FileSystem::GetAvailableSpace(const std::string& path) {
    struct statvfs st;
    if (::statvfs(path.c_str(), &st) != 0) {
        return Status::Error(ErrorCode::StorageError,
            "Failed to get available space: " + std::string(strerror(errno)));
    }
    return static_cast<size_t>(st.f_bavail) * st.f_frsize;
}

bool FileSystem::IsPathSafe(const std::string& base_path, const std::string& path) {
    // 检查 path 是否在 base_path 下
    // 规范化路径
    auto base = std::filesystem::weakly_canonical(base_path);
    auto full = std::filesystem::weakly_canonical(JoinPath(base_path, path));
    
    // 检查 full 是否在 base 下
    auto base_str = base.string();
    auto full_str = full.string();
    
    if (full_str.length() < base_str.length()) {
        return false;
    }
    
    return full_str.compare(0, base_str.length(), base_str) == 0;
}

std::string FileSystem::NormalizePath(const std::string& path) {
    return std::filesystem::weakly_canonical(path).string();
}

std::string FileSystem::JoinPath(const std::string& base, const std::string& sub) {
    return (std::filesystem::path(base) / sub).string();
}

// TempFile 实现

TempFile::TempFile(const std::string& dir, const std::string& prefix) {
    // 创建临时文件（自动清理）
    // 确保目录存在
    FileSystem::CreateDirectories(dir);
    
    // 生成唯一文件名
    path_ = FileSystem::JoinPath(dir, prefix + UUID::Generate());
    
    // 创建文件
    fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
    if (fd_ < 0) {
        throw std::runtime_error("Failed to create temp file: " + 
            std::string(strerror(errno)));
    }
}

TempFile::~TempFile() {
    // 析构时关闭并删除临时文件
    if (fd_ >= 0) {
        ::close(fd_);
    }
    if (!path_.empty()) {
        ::unlink(path_.c_str());
    }
}

TempFile::TempFile(TempFile&& other) noexcept
    : path_(std::move(other.path_)), fd_(other.fd_) {
    other.fd_ = -1;
    other.path_.clear();
}

TempFile& TempFile::operator=(TempFile&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        if (!path_.empty()) {
            ::unlink(path_.c_str());
        }
        
        path_ = std::move(other.path_);
        fd_ = other.fd_;
        
        other.fd_ = -1;
        other.path_.clear();
    }
    return *this;
}

std::string TempFile::Release() {
    std::string p = std::move(path_);
    path_.clear();
    return p;
}

void TempFile::Close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace minis3
