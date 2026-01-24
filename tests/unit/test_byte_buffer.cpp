#include <gtest/gtest.h>
#include <cstring>
#include "net/buffer/byte_buffer.h"

using namespace minis3;

// 默认构造状态
TEST(ByteBufferTest, DefaultConstruct) {
    ByteBuffer buf;
    EXPECT_EQ(buf.ReadableBytes(), 0);
    EXPECT_GT(buf.WritableBytes(), 0);
}

// 写入与读取
TEST(ByteBufferTest, WriteAndRead) {
    ByteBuffer buf;
    std::string data = "hello world";
    
    buf.Write(data.data(), data.size());
    EXPECT_EQ(buf.ReadableBytes(), data.size());
    
    std::string read_data(buf.ReadableBytes(), '\0');
    std::memcpy(read_data.data(), buf.Peek(), buf.ReadableBytes());
    
    EXPECT_EQ(read_data, data);
}

// 清空缓冲区
TEST(ByteBufferTest, Clear) {
    ByteBuffer buf;
    buf.Write("test", 4);
    EXPECT_GT(buf.ReadableBytes(), 0);
    
    buf.Clear();
    EXPECT_EQ(buf.ReadableBytes(), 0);
}

// 部分消费
TEST(ByteBufferTest, Retrieve) {
    ByteBuffer buf;
    buf.Write("hello", 5);
    
    buf.Retrieve(3);
    EXPECT_EQ(buf.ReadableBytes(), 2);
}

// 全部消费
TEST(ByteBufferTest, RetrieveAll) {
    ByteBuffer buf;
    buf.Write("hello", 5);
    
    std::string result = buf.RetrieveAllAsString();
    EXPECT_EQ(result, "hello");
    EXPECT_EQ(buf.ReadableBytes(), 0);
}
