cmake_minimum_required(VERSION 3.16)

# 查找 GTest
find_package(GTest QUIET)

if(GTest_FOUND)
    message(STATUS "GTest found, building tests")
    
    # 单元测试
    add_executable(unit_tests
        unit/test_status.cpp
        unit/test_crypto.cpp
        unit/test_uuid.cpp
        unit/test_byte_buffer.cpp
        unit/test_http_parser.cpp
        unit/test_cas_layout.cpp
    )
    target_link_libraries(unit_tests
        minis3_lib
        GTest::gtest
        GTest::gtest_main
    )
    target_include_directories(unit_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    
    # 集成测试
    add_executable(integration_tests
        integration/test_data_store.cpp
        integration/test_meta_store.cpp
    )
    target_link_libraries(integration_tests
        minis3_lib
        GTest::gtest
        GTest::gtest_main
    )
    target_include_directories(integration_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    
    # 注册测试
    add_test(NAME unit_tests COMMAND unit_tests)
    add_test(NAME integration_tests COMMAND integration_tests)
else()
    message(STATUS "GTest not found, skipping tests")
endif()
