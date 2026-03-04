#include <gtest/gtest.h>
#include "storage/cas_layout.h"
#include <filesystem>

using namespace minis3;
namespace fs = std::filesystem;

// 获取 CAS 文件路径
TEST(CasLayoutTest, GetPath) {
    CasLayout layout("/data/cas");
    
    std::string hash = "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3";
    std::string path = layout.GetCasPath(hash);
    
    // Path should be /data/cas/cas/a9/4a/<hash>.blob
    EXPECT_EQ(path, "/data/cas/cas/a9/4a/a94a8fe5ccb19ba61c4c0873d391e987982fbbd3.blob");
}

// 短 hash 的路径处理
TEST(CasLayoutTest, GetPathShortHash) {
    CasLayout layout("/data/cas");
    
    // Short hash should still work (return path with available chars)
    std::string hash = "ab";
    std::string path = layout.GetCasPath(hash);
    
    EXPECT_TRUE(path.find("/data/cas/") == 0);
}

// 获取 CAS 目录
TEST(CasLayoutTest, GetDir) {
    CasLayout layout("/data/cas");
    
    std::string hash = "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3";
    std::string dir = layout.GetCasDir(hash);
    
    // Dir should be /data/cas/cas/a9/4a
    EXPECT_EQ(dir, "/data/cas/cas/a9/4a");
}

// 获取基础目录
TEST(CasLayoutTest, BaseDir) {
    CasLayout layout("/data/cas");
    EXPECT_EQ(layout.BaseDir(), "/data/cas");
}
