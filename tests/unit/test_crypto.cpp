#include <gtest/gtest.h>
#include "util/crypto.h"

using namespace minis3;

// 空字符串 SHA256
TEST(CryptoTest, SHA256EmptyString) {
    std::string hash = Crypto::SHA256("");
    // SHA256 of empty string
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

// 常见字符串 SHA256
TEST(CryptoTest, SHA256HelloWorld) {
    std::string hash = Crypto::SHA256("hello world");
    EXPECT_EQ(hash, "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
}

// 二进制数据 SHA256
TEST(CryptoTest, SHA256Binary) {
    uint8_t data[] = {0x00, 0x01, 0x02, 0x03};
    std::string hash = Crypto::SHA256(data, sizeof(data));
    EXPECT_EQ(hash.length(), 64);  // SHA256 is 256 bits = 64 hex chars
}

// 增量计算 SHA256
TEST(CryptoTest, SHA256Context) {
    Crypto::SHA256Context ctx;
    ctx.Update("hello ");
    ctx.Update("world");
    std::string hash = ctx.Final();
    
    // Should match SHA256("hello world")
    EXPECT_EQ(hash, "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
}

// 重置上下文后再计算
TEST(CryptoTest, SHA256ContextReset) {
    Crypto::SHA256Context ctx;
    ctx.Update("test");
    ctx.Reset();
    ctx.Update("");
    std::string hash = ctx.Final();
    
    // Should match SHA256("")
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

// HMAC-SHA256
TEST(CryptoTest, HMACSHA256) {
    std::string hmac = Crypto::HMAC_SHA256("key", "data");
    EXPECT_EQ(hmac.length(), 64);  // HMAC-SHA256 output is 64 hex chars
}
