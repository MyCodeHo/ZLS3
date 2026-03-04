#!/bin/bash
# Multipart Upload 测试
# 用法: ./test_multipart.sh [server_url]

set -e

SERVER_URL=${1:-http://localhost:8080}
TOKEN="dev-token-12345"
BUCKET="multipart-test-bucket"
KEY="large-object"

echo "=== Multipart Upload Test ==="
echo "Server: $SERVER_URL"
echo ""

# 辅助函数：统一请求封装
request() {
    local method=$1
    local path=$2
    shift 2
    curl -s -X "$method" "$SERVER_URL$path" -H "Authorization: Bearer $TOKEN" "$@"
}

# 1. 创建 Bucket
echo "1. Create Bucket"
curl -s -X PUT "$SERVER_URL/buckets/$BUCKET" -H "Authorization: Bearer $TOKEN" > /dev/null
echo "   Done"

# 2. 初始化 Multipart Upload
echo ""
echo "2. Initiate Multipart Upload"
RESPONSE=$(request POST "/buckets/$BUCKET/multipart/$KEY")
echo "   Response: $RESPONSE"

# 提取 upload_id (假设 JSON 响应格式)
UPLOAD_ID=$(echo "$RESPONSE" | grep -o '"upload_id":"[^"]*"' | cut -d'"' -f4)
if [ -z "$UPLOAD_ID" ]; then
    echo "   Failed to get upload_id"
    exit 1
fi
echo "   Upload ID: $UPLOAD_ID"

# 3. 上传 Parts
echo ""
echo "3. Upload Parts"

PART1_DATA="This is part 1 of the multipart upload. "
PART2_DATA="This is part 2 of the multipart upload. "
PART3_DATA="This is part 3 of the multipart upload."

# Part 1
RESPONSE=$(curl -s -X PUT "$SERVER_URL/buckets/$BUCKET/multipart/$KEY/$UPLOAD_ID/parts/1" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/octet-stream" \
    -d "$PART1_DATA" \
    -w "\n%{http_code}")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
ETAG1=$(echo "$RESPONSE" | head -n1 | grep -o '"etag":"[^"]*"' | cut -d'"' -f4)
echo "   Part 1: HTTP $HTTP_CODE, ETag: $ETAG1"

# Part 2
RESPONSE=$(curl -s -X PUT "$SERVER_URL/buckets/$BUCKET/multipart/$KEY/$UPLOAD_ID/parts/2" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/octet-stream" \
    -d "$PART2_DATA" \
    -w "\n%{http_code}")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
ETAG2=$(echo "$RESPONSE" | head -n1 | grep -o '"etag":"[^"]*"' | cut -d'"' -f4)
echo "   Part 2: HTTP $HTTP_CODE, ETag: $ETAG2"

# Part 3
RESPONSE=$(curl -s -X PUT "$SERVER_URL/buckets/$BUCKET/multipart/$KEY/$UPLOAD_ID/parts/3" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/octet-stream" \
    -d "$PART3_DATA" \
    -w "\n%{http_code}")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
ETAG3=$(echo "$RESPONSE" | head -n1 | grep -o '"etag":"[^"]*"' | cut -d'"' -f4)
echo "   Part 3: HTTP $HTTP_CODE, ETag: $ETAG3"

# 4. Complete Multipart Upload
echo ""
echo "4. Complete Multipart Upload"
COMPLETE_BODY="{\"parts\":[{\"part_number\":1,\"etag\":\"$ETAG1\"},{\"part_number\":2,\"etag\":\"$ETAG2\"},{\"part_number\":3,\"etag\":\"$ETAG3\"}]}"
RESPONSE=$(curl -s -X POST "$SERVER_URL/buckets/$BUCKET/multipart/$KEY/$UPLOAD_ID/complete" \
    -H "Authorization: Bearer $TOKEN" \
    -H "Content-Type: application/json" \
    -d "$COMPLETE_BODY" \
    -w "\n%{http_code}")
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)
echo "   HTTP: $HTTP_CODE"
echo "   Response: $(echo "$RESPONSE" | head -n1)"

# 5. Verify Object
echo ""
echo "6. Verify Object"
RESPONSE=$(request GET "/buckets/$BUCKET/objects/$KEY")
EXPECTED="${PART1_DATA}${PART2_DATA}${PART3_DATA}"
if [ "$RESPONSE" == "$EXPECTED" ]; then
    echo "   ✓ Object content matches!"
else
    echo "   ✗ Content mismatch"
    echo "   Expected: $EXPECTED"
    echo "   Got: $RESPONSE"
fi

# 6. Cleanup
echo ""
echo "7. Cleanup"
request DELETE "/buckets/$BUCKET/objects/$KEY" > /dev/null
request DELETE "/buckets/$BUCKET" > /dev/null
echo "   Done"

echo ""
echo "=== Test Complete ==="
