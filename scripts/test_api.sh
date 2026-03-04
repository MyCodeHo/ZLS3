#!/bin/bash
# API 功能测试
# 用法: ./test_api.sh [server_url]

set -e

SERVER_URL=${1:-http://localhost:8080}
TOKEN="dev-token-12345"
BUCKET="test-bucket-$(date +%s)"
KEY="test-object"

echo "=== MiniS3 API Test ==="
echo "Server: $SERVER_URL"
echo "Bucket: $BUCKET"
echo ""

# 辅助函数：统一请求封装
request() {
    local method=$1
    local path=$2
    shift 2
    curl -s -X "$method" "$SERVER_URL$path" -H "Authorization: Bearer $TOKEN" "$@"
}

# 辅助函数：断言状态码
assert_status() {
    local expected=$1
    local actual=$2
    local msg=$3
    if [ "$actual" == "$expected" ]; then
        echo "✓ $msg"
    else
        echo "✗ $msg (expected $expected, got $actual)"
        exit 1
    fi
}

# 1. 健康检查
echo "1. Health Check"
STATUS=$(curl -s -o /dev/null -w "%{http_code}" "$SERVER_URL/healthz")
assert_status "200" "$STATUS" "Health endpoint returns 200"

# 2. 创建 Bucket
echo ""
echo "2. Create Bucket"
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X PUT "$SERVER_URL/buckets/$BUCKET" \
    -H "Authorization: Bearer $TOKEN")
assert_status "201" "$STATUS" "Create bucket returns 201"

# 3. 列出 Buckets
echo ""
echo "3. List Buckets"
RESPONSE=$(request GET "/buckets")
echo "   Response: $RESPONSE" | head -c 100
echo ""

# 4. 上传对象
echo ""
echo "4. Upload Object"
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X PUT "$SERVER_URL/buckets/$BUCKET/objects/$KEY" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: text/plain" \
    -d "Hello, MiniS3!")
assert_status "200" "$STATUS" "Put object returns 200"

# 5. 获取对象
echo ""
echo "5. Get Object"
RESPONSE=$(request GET "/buckets/$BUCKET/objects/$KEY")
if [ "$RESPONSE" == "Hello, MiniS3!" ]; then
    echo "✓ Object content matches"
else
    echo "✗ Object content mismatch: $RESPONSE"
    exit 1
fi

# 6. HEAD 对象
echo ""
echo "6. Head Object"
STATUS=$(curl -s -I -o /dev/null -w "%{http_code}" "$SERVER_URL/buckets/$BUCKET/objects/$KEY" \
    -H "Authorization: Bearer $TOKEN")
assert_status "200" "$STATUS" "Head object returns 200"

# 7. 列出对象
echo ""
echo "7. List Objects"
RESPONSE=$(request GET "/buckets/$BUCKET/objects")
echo "   Response: $RESPONSE" | head -c 100
echo ""

# 8. Range 请求
echo ""
echo "8. Range Request"
RESPONSE=$(curl -s "$SERVER_URL/buckets/$BUCKET/objects/$KEY" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Range: bytes=0-4")
if [ "$RESPONSE" == "Hello" ]; then
    echo "✓ Range request works"
else
    echo "✗ Range request failed: $RESPONSE"
fi

# 9. 删除对象
echo ""
echo "9. Delete Object"
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE "$SERVER_URL/buckets/$BUCKET/objects/$KEY" \
    -H "Authorization: Bearer $TOKEN")
assert_status "204" "$STATUS" "Delete object returns 204"

# 10. 验证删除
echo ""
echo "10. Verify Deletion"
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X GET "$SERVER_URL/buckets/$BUCKET/objects/$KEY" \
    -H "Authorization: Bearer $TOKEN")
assert_status "404" "$STATUS" "Get deleted object returns 404"

# 11. 删除 Bucket
echo ""
echo "11. Delete Bucket"
STATUS=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE "$SERVER_URL/buckets/$BUCKET" \
    -H "Authorization: Bearer $TOKEN")
assert_status "204" "$STATUS" "Delete bucket returns 204"

echo ""
echo "=== All Tests Passed ==="
