#pragma once

#include <string>

namespace minis3 {

/**
 * UUID 生成器
 */
class UUID {
public:
    /**
     * 生成随机 UUID (v4)
     */
    static std::string Generate();
    
    /**
     * 验证 UUID 格式
     */
    static bool IsValid(const std::string& uuid);
};

} // namespace minis3
