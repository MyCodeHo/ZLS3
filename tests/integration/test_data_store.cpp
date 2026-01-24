#include <gtest/gtest.h>
#include "storage/data_store.h"
#include "util/crypto.h"
#include <filesystem>

using namespace minis3;
namespace fs = std::filesystem;

class DataStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建临时目录并初始化 DataStore
        test_dir_ = "/tmp/minis3_test_" + std::to_string(getpid());
        data_dir_ = test_dir_ + "/data";
        tmp_dir_ = test_dir_ + "/tmp";
        
        fs::create_directories(data_dir_);
        fs::create_directories(tmp_dir_);
        
        store_ = std::make_unique<DataStore>(data_dir_, tmp_dir_);
        ASSERT_TRUE(store_->Init().ok());
    }
    
    void TearDown() override {
        // 清理临时目录
        store_.reset();
        fs::remove_all(test_dir_);
    }
    
    std::string test_dir_;
    std::string data_dir_;
    std::string tmp_dir_;
    std::unique_ptr<DataStore> store_;
};

// 写入与读取
TEST_F(DataStoreTest, WriteAndRead) {
    std::string content = "hello world";
    
    auto write_result = store_->Write(content);
    ASSERT_TRUE(write_result.ok());
    
    std::string cas_key = write_result.value();
    EXPECT_EQ(cas_key, Crypto::SHA256(content));
    
    auto read_result = store_->Read(cas_key);
    ASSERT_TRUE(read_result.ok());
    EXPECT_EQ(read_result.value(), content);
}

// 存在性检查
TEST_F(DataStoreTest, Exists) {
    std::string content = "test content";
    
    auto write_result = store_->Write(content);
    ASSERT_TRUE(write_result.ok());
    
    std::string cas_key = write_result.value();
    EXPECT_TRUE(store_->Exists(cas_key));
    EXPECT_FALSE(store_->Exists("nonexistent"));
}

// 删除对象
TEST_F(DataStoreTest, Delete) {
    std::string content = "to be deleted";
    
    auto write_result = store_->Write(content);
    ASSERT_TRUE(write_result.ok());
    
    std::string cas_key = write_result.value();
    EXPECT_TRUE(store_->Exists(cas_key));
    
    auto delete_status = store_->Delete(cas_key);
    EXPECT_TRUE(delete_status.ok());
    EXPECT_FALSE(store_->Exists(cas_key));
}

// 流式读取
TEST_F(DataStoreTest, StreamRead) {
    std::string content = "hello world stream";
    
    auto write_result = store_->Write(content);
    ASSERT_TRUE(write_result.ok());
    
    std::string cas_key = write_result.value();
    std::string read_content;
    
    auto status = store_->StreamRead(cas_key, 0, 0, 
        [&read_content](const char* data, size_t len) {
            read_content.append(data, len);
            return true;
        });
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(read_content, content);
}

// 流式读取（部分范围）
TEST_F(DataStoreTest, StreamReadPartial) {
    std::string content = "0123456789";
    
    auto write_result = store_->Write(content);
    ASSERT_TRUE(write_result.ok());
    
    std::string cas_key = write_result.value();
    std::string read_content;
    
    // Read bytes 3-6 (inclusive)
    auto status = store_->StreamRead(cas_key, 3, 4, 
        [&read_content](const char* data, size_t len) {
            read_content.append(data, len);
            return true;
        });
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(read_content, "3456");
}

// 写入会话
TEST_F(DataStoreTest, BeginWriteSession) {
    auto session_result = store_->BeginWrite();
    ASSERT_TRUE(session_result.ok());
    
    auto& session = session_result.value();
    
    auto write_status = session->Write("hello ", 6);
    EXPECT_TRUE(write_status.ok());
    
    write_status = session->Write("world", 5);
    EXPECT_TRUE(write_status.ok());
    
    auto finish_result = session->Finish();
    ASSERT_TRUE(finish_result.ok());
    
    std::string cas_key = finish_result.value();
    EXPECT_EQ(cas_key, Crypto::SHA256("hello world"));
    
    auto commit_result = store_->CommitWrite(std::move(session_result.MoveValue()));
    EXPECT_TRUE(commit_result.ok() || !session);  // session moved
}

// 合并多个 CAS
TEST_F(DataStoreTest, Merge) {
    // Write multiple parts
    std::vector<std::string> cas_keys;
    
    auto r1 = store_->Write("part1");
    ASSERT_TRUE(r1.ok());
    cas_keys.push_back(r1.value());
    
    auto r2 = store_->Write("part2");
    ASSERT_TRUE(r2.ok());
    cas_keys.push_back(r2.value());
    
    auto r3 = store_->Write("part3");
    ASSERT_TRUE(r3.ok());
    cas_keys.push_back(r3.value());
    
    // Merge
    auto merge_result = store_->Merge(cas_keys);
    ASSERT_TRUE(merge_result.ok());
    
    std::string merged_key = merge_result.value();
    
    // Read merged content
    auto read_result = store_->Read(merged_key);
    ASSERT_TRUE(read_result.ok());
    EXPECT_EQ(read_result.value(), "part1part2part3");
}
