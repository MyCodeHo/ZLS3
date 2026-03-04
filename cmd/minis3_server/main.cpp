#include "server/server.h"
#include "util/config.h"
#include <iostream>
#include <signal.h>
#include <spdlog/spdlog.h>

static minis3::Server* g_server = nullptr;

void SignalHandler(int signum) {
    if (g_server) {
        spdlog::info("Received signal {}, shutting down...", signum);
        g_server->Stop();
    }
}

void PrintUsage(const char* program) {
    std::cout << "Usage: " << program << " [config_file]\n"
              << "\n"
              << "MiniS3 - A lightweight object storage server\n"
              << "\n"
              << "Options:\n"
              << "  config_file    Path to configuration file (default: /etc/minis3/server.yaml)\n"
              << "  -h, --help     Show this help message\n"
              << "  -v, --version  Show version information\n"
              << std::endl;
}

void PrintVersion() {
    std::cout << "MiniS3 version 1.0.0\n"
              << "Built with C++20\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    std::string config_path = "/etc/minis3/server.yaml";
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            PrintVersion();
            return 0;
        } else if (arg[0] != '-') {
            config_path = arg;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            PrintUsage(argv[0]);
            return 1;
        }
    }
    
    // 设置信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    signal(SIGPIPE, SIG_IGN);  // 忽略 SIGPIPE，防止写入已关闭的 socket 导致进程退出
    
    try {
        // 加载配置
        spdlog::info("Loading configuration from {}...", config_path);
        minis3::Config config = minis3::Config::LoadFromFile(config_path);
        
        // 创建服务器
        minis3::Server server(config);
        g_server = &server;
        
        // 初始化
        auto status = server.Init();
        if (!status.ok()) {
            spdlog::error("Failed to initialize server: {}", status.ToString());
            return 1;
        }
        
        // 启动服务器
        server.Start();
        
        g_server = nullptr;
        return 0;
        
    } catch (const std::exception& e) {
        spdlog::error("Fatal error: {}", e.what());
        return 1;
    }
}
