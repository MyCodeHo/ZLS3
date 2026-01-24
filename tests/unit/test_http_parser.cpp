#include <gtest/gtest.h>
#include "net/http/http_parser.h"
#include "net/http/http_request.h"
#include "net/buffer/byte_buffer.h"

using namespace minis3;

// 解析简单 GET
TEST(HttpParserTest, ParseSimpleGet) {
    HttpParser parser;
    const char* request = 
        "GET /test HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    
    ByteBuffer buf;
    buf.Write(request, strlen(request));

    auto result = parser.Parse(&buf);
    
    EXPECT_TRUE(result == HttpParser::ParseResult::MESSAGE_COMPLETE);
    EXPECT_EQ(parser.Request().Method(), HttpMethod::GET);
    EXPECT_EQ(parser.Request().Path(), "/test");
}

// 解析带 body 的请求
TEST(HttpParserTest, ParseWithBody) {
    HttpParser parser;
    const char* request = 
        "PUT /bucket/key HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 11\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "hello world";
    
    ByteBuffer buf;
    buf.Write(request, strlen(request));
    
    auto result = parser.Parse(&buf);
    EXPECT_TRUE(result == HttpParser::ParseResult::HEADERS_COMPLETE ||
                result == HttpParser::ParseResult::MESSAGE_COMPLETE);

    // 继续解析 body
    result = parser.Parse(&buf);
    EXPECT_TRUE(result == HttpParser::ParseResult::MESSAGE_COMPLETE ||
                result == HttpParser::ParseResult::BODY_DATA);
    EXPECT_EQ(parser.Request().Method(), HttpMethod::PUT);
    EXPECT_EQ(parser.Request().Path(), "/bucket/key");
}

// 解析不完整请求
TEST(HttpParserTest, ParseIncomplete) {
    HttpParser parser;
    const char* request = "GET /test HTTP/1.1\r\n";
    
    ByteBuffer buf;
    buf.Write(request, strlen(request));
    
    auto result = parser.Parse(&buf);
    
    EXPECT_TRUE(result == HttpParser::ParseResult::NEED_MORE_DATA);
}

// 解析 query 参数
TEST(HttpParserTest, ParseQueryParams) {
    HttpParser parser;
    const char* request = 
        "GET /bucket?prefix=foo&max-keys=100 HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";
    
    ByteBuffer buf;
    buf.Write(request, strlen(request));
    
    auto result = parser.Parse(&buf);
    
    EXPECT_TRUE(result == HttpParser::ParseResult::MESSAGE_COMPLETE);
    EXPECT_EQ(parser.Request().Path(), "/bucket");
    EXPECT_EQ(parser.Request().GetQueryParam("prefix"), "foo");
    EXPECT_EQ(parser.Request().GetQueryParam("max-keys"), "100");
}
