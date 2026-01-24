#include <gtest/gtest.h>
#include "util/status.h"

using namespace minis3;

// 默认 OK 状态
TEST(StatusTest, DefaultIsOK) {
    Status s;
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(s.code(), ErrorCode::OK);
}

// OK 状态码
TEST(StatusTest, OKStatus) {
    Status s = Status::OK();
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(s.HttpStatus(), 200);
}

// 错误状态
TEST(StatusTest, ErrorStatus) {
    Status s = Status::Error(ErrorCode::InvalidArgument, "test error");
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.code(), ErrorCode::InvalidArgument);
    EXPECT_EQ(s.message(), "test error");
}

// NotFound 状态
TEST(StatusTest, NotFoundStatus) {
    Status s = Status::NotFound("key not found");
    EXPECT_FALSE(s.ok());
    EXPECT_TRUE(s.IsNotFound());
}

// 便捷构造方法
TEST(StatusTest, ConvenienceMethods) {
    EXPECT_FALSE(Status::InvalidArgument("test").ok());
    EXPECT_FALSE(Status::Unauthorized("test").ok());
    EXPECT_FALSE(Status::AlreadyExists("test").ok());
    EXPECT_FALSE(Status::InternalError("test").ok());
}

// Result OK
TEST(ResultTest, OKResult) {
    Result<int> r = Result<int>::Ok(42);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.value(), 42);
}

// Result Error
TEST(ResultTest, ErrorResult) {
    Result<int> r = Result<int>::Err(Status::NotFound("not found"));
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(r.status().IsNotFound());
}

// Result 隐式转换
TEST(ResultTest, ImplicitConversion) {
    Result<std::string> r = std::string("hello");
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.value(), "hello");
}

// Result bool 操作符
TEST(ResultTest, BoolOperator) {
    Result<int> ok = Result<int>::Ok(1);
    Result<int> err = Result<int>::Err(ErrorCode::InternalError);
    
    EXPECT_TRUE(static_cast<bool>(ok));
    EXPECT_FALSE(static_cast<bool>(err));
}
