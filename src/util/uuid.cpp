#include "uuid.h"
#include <regex>
#include <random>
#include <sstream>
#include <iomanip>
#ifdef HAVE_LIBUUID
#include <uuid/uuid.h>
#endif

namespace minis3 {

std::string UUID::Generate() {
#ifdef HAVE_LIBUUID
    // 优先使用系统 libuuid
    uuid_t uuid;
    uuid_generate_random(uuid);

    char str[37];
    uuid_unparse_lower(uuid, str);

    return std::string(str);
#else
    // fallback：手动生成随机 UUIDv4
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFFu);
    auto rand32 = [&]() { return dist(rng); };

    uint32_t d1 = rand32();
    uint16_t d2 = static_cast<uint16_t>(rand32());
    uint16_t d3 = static_cast<uint16_t>(rand32());
    uint16_t d4 = static_cast<uint16_t>(rand32());
    uint32_t d5 = rand32();
    uint32_t d6 = rand32();

    // 设置版本 (4) 和变体 (10xx)
    d3 = static_cast<uint16_t>((d3 & 0x0FFFu) | 0x4000u);
    d4 = static_cast<uint16_t>((d4 & 0x3FFFu) | 0x8000u);

    std::ostringstream ss;
    ss << std::hex << std::nouppercase << std::setfill('0')
       << std::setw(8) << d1 << "-"
       << std::setw(4) << d2 << "-"
       << std::setw(4) << d3 << "-"
    << std::setw(4) << d4 << "-"
    << std::setw(8) << d5
    << std::setw(4) << (d6 & 0xFFFFu);

    return ss.str();
#endif
}

bool UUID::IsValid(const std::string& uuid) {
    // 正则校验 UUID 格式
    static const std::regex uuid_regex(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$",
        std::regex::icase);
    return std::regex_match(uuid, uuid_regex);
}

} // namespace minis3
