cmake_minimum_required(VERSION 3.10)
project(wuziqi-common-lib VERSION 1.0.0 LANGUAGES CXX)

# 允许重用自己的 target
cmake_policy(SET CMP0079 NEW)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 公共库源文件
set(COMMON_SOURCES
    src/logger.cpp
    src/utils.cpp
    src/thread_pool.cpp
    src/redis_pool.cpp
    src/mysql_pool.cpp
    src/http_client.cpp
)

# 公共库头文件目录
set(COMMON_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 创建静态库
add_library(common STATIC ${COMMON_SOURCES})

# 包含目录
target_include_directories(common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# 链接库
target_link_libraries(common
    pthread
    curl
    hiredis
    mysqlclient
    ssl
    crypto
    jsoncpp
)

# 警告级别
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(common PRIVATE -Wall -Wextra)
endif()

# 安装配置
install(TARGETS common
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
)
