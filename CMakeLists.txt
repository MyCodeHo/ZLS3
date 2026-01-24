cmake_minimum_required(VERSION 3.16)
project(MiniS3 VERSION 1.0.0 LANGUAGES CXX)

# C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# 默认 Release 构建
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 导出编译命令（用于 IDE）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找依赖
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)

# 查找 MySQL
pkg_check_modules(MYSQL REQUIRED mysqlclient)

# 查找或下载 yaml-cpp
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    message(STATUS "yaml-cpp not found, using FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG yaml-cpp-0.7.0
    )
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)
endif()

# 查找或下载 spdlog
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    message(STATUS "spdlog not found, using FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.11.0
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# 查找或下载 nlohmann_json
find_package(nlohmann_json 3.10 QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, using FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# 查找 libuuid（可选）
pkg_check_modules(UUID uuid)
if(UUID_FOUND)
    add_definitions(-DHAVE_LIBUUID)
    message(STATUS "Found libuuid: ${UUID_VERSION}")
else()
    message(WARNING "libuuid not found, using fallback UUID implementation")
endif()

# 头文件目录
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include
    ${MYSQL_INCLUDE_DIRS}
    ${UUID_INCLUDE_DIRS}
)

# 链接目录
link_directories(
    ${MYSQL_LIBRARY_DIRS}
    ${UUID_LIBRARY_DIRS}
)

# 源文件
set(MINIS3_SOURCES
    # util
    src/util/config.cpp
    src/util/logging.cpp
    src/util/crypto.cpp
    src/util/uuid.cpp
    src/util/fs.cpp
    src/util/status.cpp
    src/util/metrics.cpp
    
    # net/buffer
    src/net/buffer/byte_buffer.cpp
    
    # net/epoll
    src/net/epoll/event_loop.cpp
    src/net/epoll/acceptor.cpp
    src/net/epoll/channel.cpp
    src/net/epoll/notifier.cpp
    src/net/epoll/timer_wheel.cpp
    
    # net/http
    src/net/http/http_parser.cpp
    src/net/http/http_request.cpp
    src/net/http/http_response.cpp
    src/net/http/http_router.cpp
    src/net/http/http_connection.cpp
    
    # db
    src/db/mysql_pool.cpp
    src/db/mysql_tx.cpp
    src/db/meta_store.cpp
    
    # storage
    src/storage/data_store.cpp
    src/storage/cas_layout.cpp
    src/storage/gc.cpp
    
    # service
    src/service/auth_service.cpp
    src/service/bucket_service.cpp
    src/service/object_service.cpp
    src/service/multipart_service.cpp
    src/service/presign_service.cpp
    
    # api/handlers
    src/api/handlers/bucket_handlers.cpp
    src/api/handlers/object_handlers.cpp
    src/api/handlers/multipart_handlers.cpp
    src/api/handlers/presign_handlers.cpp
    src/api/handlers/health_handlers.cpp
    
    # api/middleware
    src/api/middleware/auth_middleware.cpp
    src/api/middleware/trace_middleware.cpp
    
    # server
    src/server/server.cpp
)

# 静态库
add_library(minis3_lib STATIC ${MINIS3_SOURCES})
target_link_libraries(minis3_lib
    PUBLIC
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    ${MYSQL_LIBRARIES}
    yaml-cpp
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${UUID_LIBRARIES}
)

# 可执行文件
add_executable(minis3_server cmd/minis3_server/main.cpp)
target_link_libraries(minis3_server PRIVATE minis3_lib)

# 安装
install(TARGETS minis3_server DESTINATION bin)
install(FILES configs/server.example.yaml DESTINATION etc/minis3)

# 测试（可选）
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# 打印配置信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
