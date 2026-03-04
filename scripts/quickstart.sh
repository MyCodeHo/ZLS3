#!/bin/bash
# 快速启动开发环境

set -e

echo "===================================="
echo "MiniS3 Quick Start"
echo "===================================="

# 检查 Docker
if ! command -v docker &> /dev/null; then
    echo "Error: Docker is not installed"
    exit 1
fi

# 检测 compose 命令（v2 优先）
COMPOSE_CMD=""
if docker compose version &> /dev/null; then
    COMPOSE_CMD="docker compose"
elif command -v docker-compose &> /dev/null; then
    COMPOSE_CMD="docker-compose"
else
    echo "Error: Docker Compose is not available"
    echo "Install docker-compose or enable Docker Compose v2"
    exit 1
fi

# 停止并清理旧容器
echo "Stopping existing containers..."
cd "$(dirname "$0")/../docker"
$COMPOSE_CMD down -v 2>/dev/null || true

# 构建并启动
echo "Building and starting services..."
$COMPOSE_CMD up -d --build

# 等待服务就绪
echo "Waiting for services to be ready..."
sleep 10

# 检查 MySQL
echo "Checking MySQL..."
for i in {1..30}; do
    if $COMPOSE_CMD exec -T mysql mysqladmin ping -h localhost -u root -proot_password &> /dev/null; then
        echo "✓ MySQL is ready"
        break
    fi
    if [ $i -eq 30 ]; then
        echo "✗ MySQL failed to start"
        $COMPOSE_CMD logs mysql
        exit 1
    fi
    sleep 2
done

# 检查 MiniS3
echo "Checking MiniS3..."
for i in {1..30}; do
    if curl -f http://localhost:8080/healthz &> /dev/null; then
        echo "✓ MiniS3 is ready"
        break
    fi
    if [ $i -eq 30 ]; then
        echo "✗ MiniS3 failed to start"
        $COMPOSE_CMD logs minis3
        exit 1
    fi
    sleep 2
done

echo ""
echo "===================================="
echo "✓ All services are running!"
echo "===================================="
echo ""
echo "MiniS3 Server:  http://localhost:8080"
echo "Metrics:        http://localhost:9090/metrics"
echo "MySQL:          localhost:3306"
echo ""
echo "To view logs:"
echo "  $COMPOSE_CMD -f docker/docker-compose.yaml logs -f minis3"
echo ""
echo "To run tests:"
echo "  ./scripts/test_api.sh"
echo "  ./scripts/test_multipart.sh"
echo ""
echo "To stop:"
echo "  $COMPOSE_CMD -f docker/docker-compose.yaml down"
echo ""
