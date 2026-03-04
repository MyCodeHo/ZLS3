# 第一阶段：构建
FROM ubuntu:22.04 AS builder

# 设置非交互式安装
ENV DEBIAN_FRONTEND=noninteractive

# 安装构建依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libmysqlclient-dev \
    libssl-dev \
    libyaml-cpp-dev \
    libspdlog-dev \
    nlohmann-json3-dev \
    uuid-dev \
    pkg-config \
    curl \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /build

# 复制源代码
COPY . .

# 创建构建目录并编译
RUN mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF .. && \
    make -j$(nproc)

# 第二阶段：运行时镜像
FROM ubuntu:22.04

# 设置非交互式安装
ENV DEBIAN_FRONTEND=noninteractive

# 安装运行时依赖
RUN apt-get update && apt-get install -y \
    libmysqlclient21 \
    libssl3 \
    libyaml-cpp0.7 \
    libspdlog1 \
    uuid-runtime \
    curl \
    && rm -rf /var/lib/apt/lists/*

# 创建非 root 用户
RUN useradd -m -u 1000 minis3 && \
    mkdir -p /var/lib/minis3/data /var/lib/minis3/tmp /var/log/minis3 /etc/minis3 && \
    chown -R minis3:minis3 /var/lib/minis3 /var/log/minis3

# 从构建阶段复制可执行文件
COPY --from=builder /build/build/minis3_server /usr/local/bin/minis3_server
RUN chmod +x /usr/local/bin/minis3_server

# 复制配置文件
COPY configs/server.example.yaml /etc/minis3/server.yaml
RUN chown minis3:minis3 /etc/minis3/server.yaml

# 切换到非 root 用户
USER minis3

# 暴露端口
EXPOSE 8080 9090

# 设置工作目录
WORKDIR /var/lib/minis3

# 健康检查
HEALTHCHECK --interval=30s --timeout=3s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

# 启动服务
CMD ["/usr/local/bin/minis3_server", "/etc/minis3/server.yaml"]
