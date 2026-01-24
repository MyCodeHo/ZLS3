#include <gtest/gtest.h>
#include "util/uuid.h"
#include <set>
#include <regex>

using namespace minis3;

// UUID 格式校验
TEST(UUIDTest, GenerateFormat) {
    std::string uuid = UUID::Generate();
    
    // UUID v4 format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    // where x is any hex digit and y is one of 8, 9, a, b
    std::regex uuid_regex(
        "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$",
        std::regex_constants::icase);
    
    EXPECT_TRUE(std::regex_match(uuid, uuid_regex));
}

// UUID 唯一性
TEST(UUIDTest, GenerateUnique) {
    std::set<std::string> uuids;
    const int count = 1000;
    
    for (int i = 0; i < count; ++i) {
        uuids.insert(UUID::Generate());
    }
    
    // All UUIDs should be unique
    EXPECT_EQ(uuids.size(), count);
}

// 合法 UUID
TEST(UUIDTest, IsValidTrue) {
    EXPECT_TRUE(UUID::IsValid("550e8400-e29b-41d4-a716-446655440000"));
    EXPECT_TRUE(UUID::IsValid("550E8400-E29B-41D4-A716-446655440000"));  // uppercase
}

// 非法 UUID
TEST(UUIDTest, IsValidFalse) {
    EXPECT_FALSE(UUID::IsValid(""));
    EXPECT_FALSE(UUID::IsValid("not-a-uuid"));
    EXPECT_FALSE(UUID::IsValid("550e8400-e29b-41d4-a716"));  // too short
    EXPECT_FALSE(UUID::IsValid("550e8400-e29b-41d4-a716-446655440000-extra"));  // too long
    EXPECT_FALSE(UUID::IsValid("550e8400e29b41d4a716446655440000"));  // no dashes
}
