#pragma once

#include "status.h"
#include <string>
#include <filesystem>

namespace minis3 {

/**
 * 文件系统工具类
 */
class FileSystem {
public:
    /**
     * 创建目录（递归）
     */
    static Status CreateDirectories(const std::string& path);
    
    /**
     * 删除文件
     */
    static Status RemoveFile(const std::string& path);
    
    /**
     * 删除目录（递归）
     */
    static Status RemoveDirectory(const std::string& path);
    
    /**
     * 原子重命名文件
     */
    static Status AtomicRename(const std::string& src, const std::string& dst);
    
    /**
     * 检查文件是否存在
     */
    static bool FileExists(const std::string& path);
    
    /**
     * 检查目录是否存在
     */
    static bool DirectoryExists(const std::string& path);
    
    /**
     * 获取文件大小
     */
    static Result<size_t> GetFileSize(const std::string& path);
    
    /**
     * 获取磁盘可用空间
     */
    static Result<size_t> GetAvailableSpace(const std::string& path);
    
    /**
     * 验证路径安全性（防止路径遍历攻击）
     */
    static bool IsPathSafe(const std::string& base_path, const std::string& path);
    
    /**
     * 规范化路径
     */
    static std::string NormalizePath(const std::string& path);
    
    /**
     * 连接路径
     */
    static std::string JoinPath(const std::string& base, const std::string& sub);
};

/**
 * RAII 临时文件
 */
class TempFile {
public:
    TempFile(const std::string& dir, const std::string& prefix = "tmp_");
    ~TempFile();
    
    // 禁止拷贝
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    
    // 允许移动
    TempFile(TempFile&& other) noexcept;
    TempFile& operator=(TempFile&& other) noexcept;
    
    const std::string& Path() const { return path_; }
    int Fd() const { return fd_; }
    
    /**
     * 释放文件（不删除）
     */
    std::string Release();
    
    /**
     * 关闭文件描述符（保留文件）
     */
    void Close();

private:
    std::string path_;
    int fd_ = -1;
};

} // namespace minis3
