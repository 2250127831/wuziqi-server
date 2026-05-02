cmake_minimum_required(VERSION 3.10)
project(game-service VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.cpp
    src/handler.cpp
    src/connection_mgr.cpp
)

set(HEADERS
    include/handler.h
    include/connection_mgr.h
)

add_executable(game-service ${SOURCES} ${HEADERS})

target_include_directories(game-service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/common/include
)

target_link_libraries(game-service PRIVATE common ssl crypto)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(game-service PRIVATE -Wall -Wextra)
endif()

install(TARGETS game-service DESTINATION bin)
