#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <string_view>

namespace minis3 {

/**
 * 字节缓冲区
 * 支持高效的读写操作，避免频繁内存分配
 */
class ByteBuffer {
public:
    static constexpr size_t kInitialSize = 1024;
    static constexpr size_t kPrependSize = 8;  // 预留空间用于在头部添加数据

    explicit ByteBuffer(size_t initial_size = kInitialSize);
    
    // 可读数据大小
    size_t ReadableBytes() const { return writer_index_ - reader_index_; }
    
    // 可写空间大小
    size_t WritableBytes() const { return buffer_.size() - writer_index_; }
    
    // 预留空间大小
    size_t PrependableBytes() const { return reader_index_; }
    
    // 获取可读数据的起始指针
    const char* Peek() const { return Begin() + reader_index_; }
    
    // 获取可写空间的起始指针
    char* BeginWrite() { return Begin() + writer_index_; }
    const char* BeginWrite() const { return Begin() + writer_index_; }
    
    // 读取数据后前进读指针
    void Retrieve(size_t len);
    void RetrieveAll();
    void RetrieveUntil(const char* end);
    
    // 读取为字符串
    std::string RetrieveAsString(size_t len);
    std::string RetrieveAllAsString();
    
    // 写入数据后前进写指针
    void HasWritten(size_t len);
    
    // 回退写指针
    void Unwrite(size_t len);
    
    // 追加数据
    void Append(const char* data, size_t len);
    void Append(const std::string& str);
    void Append(std::string_view sv);
    void Append(const void* data, size_t len);

    // 兼容测试的别名
    void Write(const char* data, size_t len) { Append(data, len); }
    void Write(const std::string& str) { Append(str); }
    void Clear() { RetrieveAll(); }
    
    // 在头部添加数据
    void Prepend(const void* data, size_t len);
    
    // 确保有足够的写空间
    void EnsureWritableBytes(size_t len);
    
    // 收缩到实际大小
    void Shrink(size_t reserve = 0);
    
    // 查找 CRLF
    const char* FindCRLF() const;
    const char* FindCRLF(const char* start) const;
    
    // 查找换行符
    const char* FindEOL() const;
    const char* FindEOL(const char* start) const;
    
    // 从 fd 读取数据（使用 readv 提高效率）
    ssize_t ReadFd(int fd, int* saved_errno);
    
    // 向 fd 写入数据
    ssize_t WriteFd(int fd, int* saved_errno);
    
    // 获取内部缓冲区容量
    size_t Capacity() const { return buffer_.capacity(); }

private:
    char* Begin() { return buffer_.data(); }
    const char* Begin() const { return buffer_.data(); }
    
    void MakeSpace(size_t len);

    std::vector<char> buffer_;
    size_t reader_index_;
    size_t writer_index_;
};

} // namespace minis3
