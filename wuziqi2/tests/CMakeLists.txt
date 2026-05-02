cmake_minimum_required(VERSION 3.10)
project(wuziqi-tests VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加 jsoncpp 头文件路径
include_directories(/usr/include/jsoncpp)

# 查找 cURL
find_package(CURL REQUIRED)

# 查找 OpenSSL
find_package(OpenSSL REQUIRED)

# 添加可执行文件
add_executable(integration_test integration_test.cpp)

# 链接库 - 重要：添加 jsoncpp
target_link_libraries(integration_test PRIVATE
    ${CURL_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    jsoncpp
)

# 测试程序
add_executable(test_auth test_auth.cpp)
target_link_libraries(test_auth jsoncpp)

add_executable(test_match test_match.cpp)
target_link_libraries(test_match jsoncpp)