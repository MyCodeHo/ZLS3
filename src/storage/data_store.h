#pragma once

#include "cas_layout.h"
#include "util/status.h"
#include "util/crypto.h"
#include <string>
#include <memory>
#include <functional>

namespace minis3 {

/**
 * 写入会话 - 用于流式写入
 */
class WriteSession {
public:
    WriteSession(const std::string& tmp_path, int fd);
    ~WriteSession();
    
    // 禁止拷贝
    WriteSession(const WriteSession&) = delete;
    WriteSession& operator=(const WriteSession&) = delete;
    
    // 允许移动
    WriteSession(WriteSession&& other) noexcept;
    WriteSession& operator=(WriteSession&& other) noexcept;
    
    /**
     * 写入数据（同时计算 SHA256）
     */
    Status Write(const char* data, size_t len);
    
    /**
     * 完成写入，返回 CAS key
     */
    Result<std::string> Finish();
    
    /**
     * 取消写入（删除临时文件）
     */
    void Abort();
    
    /**
     * 获取已写入的字节数
     */
    size_t BytesWritten() const { return bytes_written_; }
    
    /**
     * 获取临时文件路径
     */
    const std::string& TmpPath() const { return tmp_path_; }

private:
    std::string tmp_path_;
    int fd_;
    size_t bytes_written_;
    Crypto::SHA256Context sha256_ctx_;
    bool finished_;
};

/**
 * DataStore - CAS 数据存储
 * 
 * 负责文件的存储和读取，基于内容寻址（CAS）
 */
class DataStore {
public:
    DataStore(const std::string& data_dir, const std::string& tmp_dir);
    ~DataStore() = default;
    
    /**
     * 初始化（创建必要的目录）
     */
    Status Init();
    
    /**
     * 开始写入会话（流式上传）
     */
    Result<std::unique_ptr<WriteSession>> BeginWrite();
    
    /**
     * 完成写入并存储到 CAS
     * @param session 写入会话
     * @param expected_sha256 可选的预期 SHA256（用于校验）
     * @return CAS key
     */
    Result<std::string> CommitWrite(std::unique_ptr<WriteSession> session,
                                     const std::string& expected_sha256 = "");
    
    /**
     * 直接写入数据（小文件）
     * @return CAS key
     */
    Result<std::string> Write(const char* data, size_t len);
    Result<std::string> Write(const std::string& data);
    
    /**
     * 读取数据
     */
    Result<std::string> Read(const std::string& cas_key);
    
    /**
     * 流式读取
     * @param cas_key CAS key
     * @param offset 起始偏移
     * @param length 读取长度（0 表示读取到文件末尾）
     * @param callback 数据回调
     */
    Status StreamRead(const std::string& cas_key, size_t offset, size_t length,
                      std::function<bool(const char* data, size_t len)> callback);
    
    /**
     * 获取文件路径（用于 sendfile）
     */
    std::string GetFilePath(const std::string& cas_key) const;
    
    /**
     * 检查 CAS 是否存在
     */
    bool Exists(const std::string& cas_key) const;
    
    /**
     * 获取 CAS 文件大小
     */
    Result<size_t> GetSize(const std::string& cas_key) const;
    
    /**
     * 删除 CAS（由 GC 调用）
     */
    Status Delete(const std::string& cas_key);
    
    /**
     * 合并多个 CAS 文件（用于 multipart complete）
     * @param cas_keys 要合并的 CAS key 列表
     * @return 合并后的新 CAS key
     */
    Result<std::string> Merge(const std::vector<std::string>& cas_keys);
    
    /**
     * 获取 CAS 布局
     */
    const CasLayout& Layout() const { return layout_; }

private:
    CasLayout layout_;
    std::string tmp_dir_;
};

} // namespace minis3
