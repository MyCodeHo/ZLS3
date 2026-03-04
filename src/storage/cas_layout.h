#pragma once

#include <string>

namespace minis3 {

/**
 * CAS 路径布局
 * 
 * 根据 SHA256 哈希值生成存储路径
 * 路径格式: {base}/cas/aa/bb/<sha256>.blob
 */
class CasLayout {
public:
    explicit CasLayout(std::string base_dir);
    
    /**
     * 获取 CAS 文件路径
     * @param cas_key SHA256 哈希值（64字符十六进制）
     */
    std::string GetCasPath(const std::string& cas_key) const;
    
    /**
     * 获取 CAS 目录路径（不含文件名）
     */
    std::string GetCasDir(const std::string& cas_key) const;
    
    /**
     * 获取基础目录
     */
    const std::string& BaseDir() const { return base_dir_; }
    
    /**
     * 验证 CAS key 格式
     */
    static bool IsValidCasKey(const std::string& cas_key);

private:
    std::string base_dir_;
};

} // namespace minis3
