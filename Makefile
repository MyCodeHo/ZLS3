.PHONY: all build clean test docker-build docker-up docker-down docker-logs help

# 默认目标
all: build

# 构建项目
build:
	@echo "Building MiniS3..."
	@mkdir -p build
	@cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$$(nproc)
	@echo "Build complete: ./build/minis3_server"

# 调试构建
debug:
	@echo "Building MiniS3 (Debug)..."
	@mkdir -p build
	@cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j$$(nproc)
	@echo "Debug build complete"

# 清理构建文件
clean:
	@echo "Cleaning build files..."
	@rm -rf build
	@echo "Clean complete"

# 运行单元测试
test: build
	@echo "Running tests..."
	@cd build && ctest --output-on-failure

# 格式化代码 (需要 clang-format)
format:
	@echo "Formatting code..."
	@find src tests -name "*.cpp" -o -name "*.h" | xargs clang-format -i
	@echo "Format complete"

# Docker 相关命令
docker-build:
	@echo "Building Docker image..."
	@cd docker && docker-compose build

docker-up:
	@echo "Starting services..."
	@cd docker && docker-compose up -d
	@echo "Waiting for services..."
	@sleep 5
	@echo "Services started. Check status with: make docker-logs"

docker-down:
	@echo "Stopping services..."
	@cd docker && docker-compose down
	@echo "Services stopped"

docker-logs:
	@cd docker && docker-compose logs -f minis3

docker-clean:
	@echo "Cleaning Docker resources..."
	@cd docker && docker-compose down -v
	@echo "Docker resources cleaned"

# 快速启动（构建 + Docker）
quickstart:
	@chmod +x scripts/quickstart.sh
	@./scripts/quickstart.sh

# 运行集成测试
integration-test:
	@echo "Running integration tests..."
	@chmod +x scripts/test_api.sh scripts/test_multipart.sh
	@./scripts/test_api.sh
	@./scripts/test_multipart.sh

# 性能测试
benchmark:
	@echo "Running benchmarks..."
	@chmod +x scripts/bench_upload.sh scripts/bench_download.sh
	@./scripts/bench_upload.sh
	@./scripts/bench_download.sh

# 安装依赖 (Ubuntu/Debian)
install-deps:
	@echo "Installing dependencies..."
	@sudo apt-get update
	@sudo apt-get install -y \
		build-essential \
		cmake \
		libmysqlclient-dev \
		libssl-dev \
		libyaml-cpp-dev \
		libspdlog-dev \
		nlohmann-json3-dev \
		uuid-dev \
		pkg-config \
		clang-format
	@echo "Dependencies installed"

# 帮助信息
help:
	@echo "MiniS3 Makefile Commands:"
	@echo ""
	@echo "Build:"
	@echo "  make build           - Build the project (Release)"
	@echo "  make debug           - Build the project (Debug)"
	@echo "  make clean           - Clean build files"
	@echo "  make test            - Run unit tests"
	@echo "  make format          - Format source code"
	@echo ""
	@echo "Docker:"
	@echo "  make docker-build    - Build Docker image"
	@echo "  make docker-up       - Start services with Docker"
	@echo "  make docker-down     - Stop Docker services"
	@echo "  make docker-logs     - View service logs"
	@echo "  make docker-clean    - Clean Docker volumes"
	@echo "  make quickstart      - Quick start (recommended)"
	@echo ""
	@echo "Testing:"
	@echo "  make integration-test - Run integration tests"
	@echo "  make benchmark        - Run performance benchmarks"
	@echo ""
	@echo "Setup:"
	@echo "  make install-deps    - Install system dependencies"
	@echo ""
