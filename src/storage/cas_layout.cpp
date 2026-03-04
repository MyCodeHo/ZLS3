#include "cas_layout.h"
#include "util/fs.h"
#include <regex>

namespace minis3 {

CasLayout::CasLayout(std::string base_dir)
    : base_dir_(std::move(base_dir)) {
}

std::string CasLayout::GetCasPath(const std::string& cas_key) const {
    // 路径格式: {base}/cas/aa/bb/<sha256>.blob
    // 使用前4个字符做两级目录
    return FileSystem::JoinPath(GetCasDir(cas_key), cas_key + ".blob");
}

std::string CasLayout::GetCasDir(const std::string& cas_key) const {
    // 兜底目录，避免 key 过短
    if (cas_key.size() < 4) {
        return FileSystem::JoinPath(base_dir_, "cas/00/00");
    }
    
    // 分两级目录以减少单目录文件数
    std::string dir = FileSystem::JoinPath(base_dir_, "cas");
    dir = FileSystem::JoinPath(dir, cas_key.substr(0, 2));
    dir = FileSystem::JoinPath(dir, cas_key.substr(2, 2));
    
    return dir;
}

bool CasLayout::IsValidCasKey(const std::string& cas_key) {
    // 必须是 64 位 hex 字符串
    if (cas_key.size() != 64) {
        return false;
    }
    
    for (char c : cas_key) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    
    return true;
}

} // namespace minis3
