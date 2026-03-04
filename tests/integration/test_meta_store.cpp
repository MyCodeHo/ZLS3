#include <gtest/gtest.h>
#include "db/meta_store.h"
#include "db/mysql_pool.h"

using namespace minis3;

// 注意：这些测试需要运行中的 MySQL 实例
// 跳过条件：如果 MySQL 不可用

class MetaStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 初始化 MySQL 连接池并创建 MetaStore
        // 尝试连接测试数据库
        MySQLConfig config;
        config.host = "127.0.0.1";
        config.port = 3306;
        config.user = "minis3_test";
        config.password = "test_password";
        config.database = "minis3_test";
        config.pool_size = 2;
        
        pool_ = std::make_unique<MySQLPool>(config);
        auto status = pool_->Init();
        
        if (!status) {
            GTEST_SKIP() << "MySQL not available, skipping integration tests";
        }
        
        store_ = std::make_unique<MetaStore>(*pool_);
        
        // 清理测试数据
        CleanupTestData();
    }
    
    void TearDown() override {
        // 清理测试数据
        if (store_) {
            CleanupTestData();
        }
    }
    
    void CleanupTestData() {
        // 删除测试创建的桶和对象（占位）
        // 删除测试创建的桶和对象
        // 实际实现需要直接 SQL 清理
    }
    
    std::unique_ptr<MySQLPool> pool_;
    std::unique_ptr<MetaStore> store_;
};

// 创建并查询 bucket
TEST_F(MetaStoreTest, CreateAndGetBucket) {
    auto create_result = store_->CreateBucket("test-bucket", "test-owner");
    ASSERT_TRUE(create_result.ok());
    
    auto get_result = store_->GetBucket("test-bucket");
    ASSERT_TRUE(get_result.ok());
    
    const auto& bucket = get_result.value();
    EXPECT_EQ(bucket.name, "test-bucket");
    EXPECT_TRUE(bucket.owner_id.empty());
}

// 重复创建 bucket
TEST_F(MetaStoreTest, CreateDuplicateBucket) {
    auto result1 = store_->CreateBucket("dup-bucket", "owner");
    ASSERT_TRUE(result1.ok());
    
    auto result2 = store_->CreateBucket("dup-bucket", "owner");
    EXPECT_FALSE(result2.ok());
}

// 删除 bucket
TEST_F(MetaStoreTest, DeleteBucket) {
    auto create_result = store_->CreateBucket("to-delete", "owner");
    ASSERT_TRUE(create_result.ok());
    
    auto delete_status = store_->DeleteBucket("to-delete");
    EXPECT_TRUE(delete_status.ok());
    
    auto get_result = store_->GetBucket("to-delete");
    EXPECT_FALSE(get_result.ok());
}

// 列出 buckets
TEST_F(MetaStoreTest, ListBuckets) {
    store_->CreateBucket("list-bucket-1", "list-owner");
    store_->CreateBucket("list-bucket-2", "list-owner");
    store_->CreateBucket("other-bucket", "other-owner");
    
    auto result = store_->ListBuckets("list-owner");
    ASSERT_TRUE(result.ok());
    
    const auto& buckets = result.value();
    EXPECT_GE(buckets.size(), 2);
    
    bool found1 = false, found2 = false;
    for (const auto& b : buckets) {
        if (b.name == "list-bucket-1") found1 = true;
        if (b.name == "list-bucket-2") found2 = true;
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found2);
}

// 创建并查询对象
TEST_F(MetaStoreTest, PutAndGetObject) {
    auto bucket_result = store_->CreateBucket("obj-bucket", "owner");
    ASSERT_TRUE(bucket_result.ok());
    int64_t bucket_id = bucket_result.value();
    
    auto put_result = store_->PutObject(bucket_id, "test-key", 100, "\"etag\"",
                                         "text/plain", "sha256hash", "owner");
    ASSERT_TRUE(put_result.ok());
    
    auto get_result = store_->GetObject(bucket_id, "test-key");
    ASSERT_TRUE(get_result.ok());
    
    const auto& obj = get_result.value();
    EXPECT_EQ(obj.key, "test-key");
    EXPECT_EQ(obj.size, 100);
    EXPECT_EQ(obj.content_type, "text/plain");
}

// 删除对象
TEST_F(MetaStoreTest, DeleteObject) {
    auto bucket_result = store_->CreateBucket("del-obj-bucket", "owner");
    ASSERT_TRUE(bucket_result.ok());
    int64_t bucket_id = bucket_result.value();
    
    store_->PutObject(bucket_id, "to-delete", 50, "\"etag\"",
                      "text/plain", "sha256", "owner");
    
    auto delete_status = store_->DeleteObject(bucket_id, "to-delete");
    EXPECT_TRUE(delete_status.ok());
    
    auto get_result = store_->GetObject(bucket_id, "to-delete");
    EXPECT_FALSE(get_result.ok());
}

// 列出对象
TEST_F(MetaStoreTest, ListObjects) {
    auto bucket_result = store_->CreateBucket("list-obj-bucket", "owner");
    ASSERT_TRUE(bucket_result.ok());
    int64_t bucket_id = bucket_result.value();
    
    store_->PutObject(bucket_id, "prefix/a", 10, "\"1\"", "text/plain", "h1", "owner");
    store_->PutObject(bucket_id, "prefix/b", 20, "\"2\"", "text/plain", "h2", "owner");
    store_->PutObject(bucket_id, "other/c", 30, "\"3\"", "text/plain", "h3", "owner");
    
    auto result = store_->ListObjects(bucket_id, "prefix/", "", "", 100);
    ASSERT_TRUE(result.ok());
    
    const auto& list_result = result.value();
    EXPECT_EQ(list_result.objects.size(), 2);
}

// CAS 引用计数增减
TEST_F(MetaStoreTest, CasBlobRefCount) {
    std::string hash = "test_hash_for_refcount";
    
    auto register_result = store_->RegisterCasBlob(hash, 1000);
    ASSERT_TRUE(register_result.ok());
    
    auto get_result = store_->GetCasBlob(hash);
    ASSERT_TRUE(get_result.ok());
    EXPECT_EQ(get_result.value().ref_count, 1);
    
    auto inc_status = store_->IncrementRefCount(hash);
    EXPECT_TRUE(inc_status.ok());
    
    get_result = store_->GetCasBlob(hash);
    EXPECT_EQ(get_result.value().ref_count, 2);
    
    auto dec_status = store_->DecrementRefCount(hash);
    EXPECT_TRUE(dec_status.ok());
    
    get_result = store_->GetCasBlob(hash);
    EXPECT_EQ(get_result.value().ref_count, 1);
}
