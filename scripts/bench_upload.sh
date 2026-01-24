#!/bin/bash
# 上传性能测试
# 用法: ./bench_upload.sh <server_url> <bucket> <file> [concurrency]

set -e

SERVER_URL=${1:-http://localhost:8080}
BUCKET=${2:-default}
FILE=${3:-test_file.bin}
CONCURRENCY=${4:-10}
ITERATIONS=${5:-100}

if [ ! -f "$FILE" ]; then
    echo "Error: File not found: $FILE"
    exit 1
fi

FILE_SIZE=$(stat -c%s "$FILE")
echo "=== Upload Benchmark ==="
echo "Server: $SERVER_URL"
echo "Bucket: $BUCKET"
echo "File: $FILE ($(numfmt --to=iec $FILE_SIZE))"
echo "Concurrency: $CONCURRENCY"
echo "Iterations: $ITERATIONS"
echo ""

# 创建 bucket (忽略已存在错误)
curl -s -X PUT "$SERVER_URL/buckets/$BUCKET" -H "Authorization: Bearer dev-token-12345" > /dev/null 2>&1 || true

# 准备测试临时目录
TMP_DIR=$(mktemp -d)
trap "rm -rf $TMP_DIR" EXIT

# 生成测试 URL 列表
for i in $(seq 1 $ITERATIONS); do
    echo "$SERVER_URL/buckets/$BUCKET/objects/bench_object_$i"
done > "$TMP_DIR/urls.txt"

echo "Starting upload benchmark..."
START_TIME=$(date +%s.%N)

# 使用 xargs 并行上传
cat "$TMP_DIR/urls.txt" | xargs -P $CONCURRENCY -I {} \
    curl -s -X PUT {} \
    -H "Authorization: Bearer dev-token-12345" \
    -H "Content-Type: application/octet-stream" \
    --data-binary @"$FILE" \
    -w "%{http_code}\n" -o /dev/null

END_TIME=$(date +%s.%N)
DURATION=$(echo "$END_TIME - $START_TIME" | bc)

# 计算统计
TOTAL_BYTES=$(echo "$FILE_SIZE * $ITERATIONS" | bc)
THROUGHPUT=$(echo "scale=2; $TOTAL_BYTES / $DURATION / 1024 / 1024" | bc)
OPS_PER_SEC=$(echo "scale=2; $ITERATIONS / $DURATION" | bc)

echo ""
echo "=== Results ==="
echo "Total time: ${DURATION}s"
echo "Total data: $(numfmt --to=iec $TOTAL_BYTES)"
echo "Throughput: ${THROUGHPUT} MB/s"
echo "Operations: ${OPS_PER_SEC} ops/s"

# 清理测试对象
echo ""
echo "Cleaning up..."
for i in $(seq 1 $ITERATIONS); do
    curl -s -X DELETE "$SERVER_URL/buckets/$BUCKET/objects/bench_object_$i" \
        -H "Authorization: Bearer dev-token-12345" > /dev/null 2>&1 &
done
wait
echo "Done."
