#!/bin/bash
# 安装 MiniS3 所需依赖

set -e

echo "=== Installing MiniS3 dependencies ==="

# 更新包列表
echo "Updating package list..."
sudo apt-get update -qq

# 安装基础依赖
echo "Installing basic dependencies..."
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    git

# 安装 OpenSSL
echo "Installing OpenSSL..."
sudo apt-get install -y \
    libssl-dev

# 安装 MySQL client库
echo "Installing MySQL client library..."
sudo apt-get install -y \
    libmysqlclient-dev \
    mysql-client

# 安装 libuuid
echo "Installing libuuid..."
sudo apt-get install -y \
    uuid-dev

# 安装 yaml-cpp
echo "Installing yaml-cpp..."
if ! dpkg -l | grep -q libyaml-cpp-dev; then
    sudo apt-get install -y libyaml-cpp-dev
fi

# 安装 spdlog
echo "Installing spdlog..."
if ! dpkg -l | grep -q libspdlog-dev; then
    sudo apt-get install -y libspdlog-dev
fi

# 安装 nlohmann-json
echo "Installing nlohmann-json..."
if ! dpkg -l | grep -q nlohmann-json3-dev; then
    sudo apt-get install -y nlohmann-json3-dev
fi

echo ""
echo "=== Dependencies installation complete ==="
echo ""
echo "Installed packages:"
echo "  - OpenSSL:       $(openssl version)"
echo "  - MySQL client:  $(mysql --version | head -1)"
echo "  - CMake:         $(cmake --version | head -1)"
echo "  - g++:           $(g++ --version | head -1)"
echo ""
echo "You can now build the project with:"
echo "  mkdir -p build && cd build"
echo "  cmake .."
echo "  make -j\$(nproc)"
