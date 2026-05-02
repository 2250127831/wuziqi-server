cmake_minimum_required(VERSION 3.10)
project(auth-service VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(SOURCES
    src/main.cpp
    src/handler.cpp
)

set(HEADERS
    include/handler.h
)

add_executable(auth-service ${SOURCES} ${HEADERS})

# 包含目录
target_include_directories(auth-service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/common/include
)

# 链接公共库
target_link_libraries(auth-service PRIVATE common)

# 警告级别
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(auth-service PRIVATE -Wall -Wextra)
endif()

install(TARGETS auth-service DESTINATION bin)
install(FILES config/service.conf DESTINATION etc/wuziqi OPTIONAL)
