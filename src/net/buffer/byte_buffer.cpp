#include "byte_buffer.h"
#include <sys/uio.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>

namespace minis3 {

ByteBuffer::ByteBuffer(size_t initial_size)
    : buffer_(kPrependSize + initial_size)
    , reader_index_(kPrependSize)
    , writer_index_(kPrependSize) {
}

void ByteBuffer::Retrieve(size_t len) {
    // 消费已读数据
    if (len < ReadableBytes()) {
        reader_index_ += len;
    } else {
        RetrieveAll();
    }
}

void ByteBuffer::RetrieveAll() {
    // 清空缓冲区
    reader_index_ = kPrependSize;
    writer_index_ = kPrependSize;
}

void ByteBuffer::RetrieveUntil(const char* end) {
    Retrieve(end - Peek());
}

std::string ByteBuffer::RetrieveAsString(size_t len) {
    std::string result(Peek(), std::min(len, ReadableBytes()));
    Retrieve(len);
    return result;
}

std::string ByteBuffer::RetrieveAllAsString() {
    return RetrieveAsString(ReadableBytes());
}

void ByteBuffer::HasWritten(size_t len) {
    writer_index_ += len;
}

void ByteBuffer::Unwrite(size_t len) {
    writer_index_ -= len;
}

void ByteBuffer::Append(const char* data, size_t len) {
    // 追加数据到缓冲区
    EnsureWritableBytes(len);
    std::copy(data, data + len, BeginWrite());
    HasWritten(len);
}

void ByteBuffer::Append(const std::string& str) {
    Append(str.data(), str.size());
}

void ByteBuffer::Append(std::string_view sv) {
    Append(sv.data(), sv.size());
}

void ByteBuffer::Append(const void* data, size_t len) {
    Append(static_cast<const char*>(data), len);
}

void ByteBuffer::Prepend(const void* data, size_t len) {
    // 在可 prepend 区域前置数据
    reader_index_ -= len;
    std::memcpy(Begin() + reader_index_, data, len);
}

void ByteBuffer::EnsureWritableBytes(size_t len) {
    // 确保可写空间足够
    if (WritableBytes() < len) {
        MakeSpace(len);
    }
}

void ByteBuffer::Shrink(size_t reserve) {
    // 缩容并保留一定空闲空间
    std::vector<char> buf(kPrependSize + ReadableBytes() + reserve);
    std::copy(Peek(), Peek() + ReadableBytes(), buf.data() + kPrependSize);
    buf.swap(buffer_);
    reader_index_ = kPrependSize;
    writer_index_ = reader_index_ + ReadableBytes();
}

const char* ByteBuffer::FindCRLF() const {
    // 查找 \r\n
    const char* crlf = std::search(Peek(), BeginWrite(), "\r\n", "\r\n" + 2);
    return crlf == BeginWrite() ? nullptr : crlf;
}

const char* ByteBuffer::FindCRLF(const char* start) const {
    const char* crlf = std::search(start, BeginWrite(), "\r\n", "\r\n" + 2);
    return crlf == BeginWrite() ? nullptr : crlf;
}

const char* ByteBuffer::FindEOL() const {
    const void* eol = std::memchr(Peek(), '\n', ReadableBytes());
    return static_cast<const char*>(eol);
}

const char* ByteBuffer::FindEOL(const char* start) const {
    const void* eol = std::memchr(start, '\n', BeginWrite() - start);
    return static_cast<const char*>(eol);
}

ssize_t ByteBuffer::ReadFd(int fd, int* saved_errno) {
    // 使用栈上的额外缓冲区，配合 readv 一次性读取更多数据
    char extrabuf[65536];
    
    struct iovec vec[2];
    const size_t writable = WritableBytes();
    
    vec[0].iov_base = BeginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    
    // 如果 buffer 可写空间足够大，就不使用 extrabuf
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);
    
    if (n < 0) {
        *saved_errno = errno;
    } else if (static_cast<size_t>(n) <= writable) {
        writer_index_ += n;
    } else {
        writer_index_ = buffer_.size();
        Append(extrabuf, n - writable);
    }
    
    return n;
}

ssize_t ByteBuffer::WriteFd(int fd, int* saved_errno) {
    // 写出可读数据并更新读指针
    ssize_t n = ::write(fd, Peek(), ReadableBytes());
    if (n < 0) {
        *saved_errno = errno;
    } else {
        Retrieve(n);
    }
    return n;
}

void ByteBuffer::MakeSpace(size_t len) {
    // 扩容或整理已读数据以腾出空间
    if (WritableBytes() + PrependableBytes() < len + kPrependSize) {
        // 空间不够，需要扩容
        buffer_.resize(writer_index_ + len);
    } else {
        // 把已读的空间让出来
        size_t readable = ReadableBytes();
        std::copy(Begin() + reader_index_, Begin() + writer_index_, 
                  Begin() + kPrependSize);
        reader_index_ = kPrependSize;
        writer_index_ = reader_index_ + readable;
    }
}

} // namespace minis3
