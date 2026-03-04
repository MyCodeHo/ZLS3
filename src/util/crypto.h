#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace minis3 {

/**
 * 加密工具类
 */
class Crypto {
public:
    /**
     * 计算 SHA256 哈希值（十六进制字符串）
     */
    static std::string SHA256(const std::string& data);
    static std::string SHA256(const uint8_t* data, size_t len);
    
    /**
     * 计算文件的 SHA256 哈希值
     */
    static std::string SHA256File(const std::string& filepath);
    
    /**
     * 计算 HMAC-SHA256
     */
    static std::string HMAC_SHA256(const std::string& key, const std::string& data);
    
    /**
     * SHA256 上下文（用于流式计算）
     */
    class SHA256Context {
    public:
        SHA256Context();
        ~SHA256Context();
        
        // 禁止拷贝
        SHA256Context(const SHA256Context&) = delete;
        SHA256Context& operator=(const SHA256Context&) = delete;
        
        // 允许移动
        SHA256Context(SHA256Context&& other) noexcept;
        SHA256Context& operator=(SHA256Context&& other) noexcept;
        
        /**
         * 更新哈希值
         */
        void Update(const uint8_t* data, size_t len);
        void Update(const std::string& data);
        
        /**
         * 完成计算并返回十六进制字符串
         */
        std::string Final();
        
        /**
         * 重置上下文
         */
        void Reset();

    private:
        void* ctx_;  // EVP_MD_CTX*
    };
    
private:
    static std::string ToHexString(const uint8_t* data, size_t len);
};

} // namespace minis3
