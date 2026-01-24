#!/bin/bash
# 下载性能测试
# 用法: ./bench_download.sh <server_url> <bucket> <key> [concurrency] [iterations]

set -e

SERVER_URL=${1:-http://localhost:8080}
BUCKET=${2:-default}
KEY=${3:-test-object}
CONCURRENCY=${4:-10}
ITERATIONS=${5:-100}

echo "=== Download Benchmark ==="
echo "Server: $SERVER_URL"
echo "Bucket: $BUCKET"
echo "Key: $KEY"
echo "Concurrency: $CONCURRENCY"
echo "Iterations: $ITERATIONS"
echo ""

# 检查对象是否存在
HTTP_CODE=$(curl -s -I -o /dev/null -w "%{http_code}" "$SERVER_URL/buckets/$BUCKET/objects/$KEY" \
    -H "Authorization: Bearer dev-token-12345")

if [ "$HTTP_CODE" != "200" ]; then
    echo "Error: Object not found (HTTP $HTTP_CODE)"
    exit 1
fi

# 获取对象大小
CONTENT_LENGTH=$(curl -s -I "$SERVER_URL/buckets/$BUCKET/objects/$KEY" \
    -H "Authorization: Bearer dev-token-12345" | grep -i content-length | awk '{print $2}' | tr -d '\r')

echo "Object size: $(numfmt --to=iec ${CONTENT_LENGTH:-0})"
echo ""

# 准备测试临时目录
TMP_DIR=$(mktemp -d)
trap "rm -rf $TMP_DIR" EXIT

echo "Starting download benchmark..."
START_TIME=$(date +%s.%N)

# 并行下载
for i in $(seq 1 $ITERATIONS); do
    echo "$i"
done | xargs -P $CONCURRENCY -I {} \
    curl -s -o /dev/null "$SERVER_URL/buckets/$BUCKET/objects/$KEY" \
    -H "Authorization: Bearer dev-token-12345" \
    -w "%{http_code} %{time_total}\n" >> "$TMP_DIR/results.txt"

END_TIME=$(date +%s.%N)
DURATION=$(echo "$END_TIME - $START_TIME" | bc)

# 计算统计
SUCCESS_COUNT=$(grep "^200" "$TMP_DIR/results.txt" | wc -l)
FAIL_COUNT=$((ITERATIONS - SUCCESS_COUNT))

if [ -n "$CONTENT_LENGTH" ] && [ "$CONTENT_LENGTH" -gt 0 ]; then
    TOTAL_BYTES=$(echo "$CONTENT_LENGTH * $SUCCESS_COUNT" | bc)
    THROUGHPUT=$(echo "scale=2; $TOTAL_BYTES / $DURATION / 1024 / 1024" | bc)
else
    TOTAL_BYTES=0
    THROUGHPUT="N/A"
fi

OPS_PER_SEC=$(echo "scale=2; $ITERATIONS / $DURATION" | bc)

# 计算延迟统计
if [ -f "$TMP_DIR/results.txt" ]; then
    LATENCIES=$(grep "^200" "$TMP_DIR/results.txt" | awk '{print $2}' | sort -n)
    
    if [ -n "$LATENCIES" ]; then
        P50=$(echo "$LATENCIES" | awk -v n=$SUCCESS_COUNT 'NR==int(n*0.50){print}')
        P95=$(echo "$LATENCIES" | awk -v n=$SUCCESS_COUNT 'NR==int(n*0.95){print}')
        P99=$(echo "$LATENCIES" | awk -v n=$SUCCESS_COUNT 'NR==int(n*0.99){print}')
        AVG=$(echo "$LATENCIES" | awk '{sum+=$1} END {printf "%.3f", sum/NR}')
    fi
fi

echo ""
echo "=== Results ==="
echo "Total time: ${DURATION}s"
echo "Success: $SUCCESS_COUNT / $ITERATIONS"
echo "Failed: $FAIL_COUNT"
echo "Total data: $(numfmt --to=iec ${TOTAL_BYTES:-0})"
echo "Throughput: ${THROUGHPUT} MB/s"
echo "Operations: ${OPS_PER_SEC} ops/s"
echo ""
echo "=== Latency ==="
echo "Avg: ${AVG:-N/A}s"
echo "P50: ${P50:-N/A}s"
echo "P95: ${P95:-N/A}s"
echo "P99: ${P99:-N/A}s"
