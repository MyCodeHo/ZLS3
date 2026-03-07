#include "storage/data_store.h"
#include "util/fs.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace minis3;
namespace fs = std::filesystem;

namespace {

struct BenchResult {
    double seconds = 0.0;
    double mbps = 0.0;
    size_t total_bytes = 0;
};

BenchResult RunBench(const std::string& root,
                     SyncMode sync_mode,
                     size_t object_bytes,
                     int object_count) {
    std::string mode_name = DataStore::SyncModeToString(sync_mode);
    std::string data_dir = root + "/data_" + mode_name;
    std::string tmp_dir = root + "/tmp_" + mode_name;

    fs::remove_all(data_dir);
    fs::remove_all(tmp_dir);
    fs::create_directories(data_dir);
    fs::create_directories(tmp_dir);

    DataStore store(data_dir, tmp_dir, sync_mode);
    auto init_status = store.Init();
    if (!init_status.ok()) {
        throw std::runtime_error("DataStore init failed: " + init_status.message());
    }

    std::string payload(object_bytes, 'x');
    std::vector<std::string> keys;
    keys.reserve(static_cast<size_t>(object_count));

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < object_count; ++i) {
        payload[0] = static_cast<char>('a' + (i % 26));
        auto write_result = store.Write(payload);
        if (!write_result.ok()) {
            throw std::runtime_error("Write failed: " + write_result.status().message());
        }
        keys.push_back(write_result.value());
    }
    auto end = std::chrono::steady_clock::now();

    size_t total_bytes = object_bytes * static_cast<size_t>(object_count);
    double seconds = std::chrono::duration<double>(end - start).count();
    double mbps = seconds > 0.0 ? (static_cast<double>(total_bytes) / (1024.0 * 1024.0)) / seconds : 0.0;

    return BenchResult{seconds, mbps, total_bytes};
}

void PrintResult(const std::string& mode, const BenchResult& r) {
    std::cout << "mode=" << mode
              << " total_bytes=" << r.total_bytes
              << " elapsed_s=" << r.seconds
              << " throughput_MBps=" << r.mbps
              << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        size_t object_bytes = 64 * 1024;
        int object_count = 300;
        std::string root = "/tmp/minis3_sync_bench";

        if (argc > 1) {
            object_bytes = static_cast<size_t>(std::stoull(argv[1]));
        }
        if (argc > 2) {
            object_count = std::stoi(argv[2]);
        }
        if (argc > 3) {
            root = argv[3];
        }

        fs::create_directories(root);

        auto fsync_res = RunBench(root, SyncMode::FSYNC, object_bytes, object_count);
        auto fdatasync_res = RunBench(root, SyncMode::FDATASYNC, object_bytes, object_count);
        auto none_res = RunBench(root, SyncMode::NONE, object_bytes, object_count);

        PrintResult("fsync", fsync_res);
        PrintResult("fdatasync", fdatasync_res);
        PrintResult("none", none_res);

        std::cout << "speedup_fdatasync_vs_fsync="
                  << (fsync_res.seconds > 0 ? fsync_res.seconds / fdatasync_res.seconds : 0)
                  << std::endl;
        std::cout << "speedup_none_vs_fsync="
                  << (fsync_res.seconds > 0 ? fsync_res.seconds / none_res.seconds : 0)
                  << std::endl;

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "bench failed: " << ex.what() << std::endl;
        return 1;
    }
}
