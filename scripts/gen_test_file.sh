#!/bin/bash
# 生成测试文件
# 用法: ./gen_test_file.sh <size_mb> <output_file>

set -e

SIZE_MB=${1:-10}
OUTPUT_FILE=${2:-test_file.bin}

echo "Generating ${SIZE_MB}MB test file: ${OUTPUT_FILE}"

# 使用 /dev/urandom 生成随机内容
dd if=/dev/urandom of="${OUTPUT_FILE}" bs=1M count="${SIZE_MB}" 2>/dev/null

# 统计大小与校验和
SIZE=$(ls -lh "${OUTPUT_FILE}" | awk '{print $5}')
SHA256=$(sha256sum "${OUTPUT_FILE}" | cut -d' ' -f1)

echo "Generated file:"
echo "  Size: ${SIZE}"
echo "  SHA256: ${SHA256}"
