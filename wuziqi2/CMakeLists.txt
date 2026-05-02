/**
 * @file CMakeLists.txt
 * @brief 五子棋游戏项目构建配置
 * 
 * =====================================================
 * CMake 是什么？
 * =====================================================
 * 
 * CMake 是一个跨平台的构建系统工具。
 * 
 * 工作流程：
 * 
 *   1. 编写 CMakeLists.txt（构建配置）
 *   2. 运行 cmake ..（生成 Makefile）
 *   3. 运行 make（编译）
 * 
 *   ┌──────────────────┐
 *   │  CMakeLists.txt   │
 *   │  (构建配置)        │
 *   └────────┬─────────┘
 *            │ cmake
 *            ▼
 *   ┌──────────────────┐
 *   │    Makefile       │
 *   │  (平台相关)        │
 *   └────────┬─────────┘
 *            │ make
 *            ▼
 *   ┌──────────────────┐
 *   │   可执行文件       │
 *   │  (二进制程序)      │
 *   └──────────────────┘
 * 
 * =====================================================
 * 项目结构
 * =====================================================
 * 
 *   wuziqi2/                    # 项目根目录
 *   ├── CMakeLists.txt         # 根配置（定义通用变量）
 *   ├── common/                 # 公共模块
 *   │   ├── include/            # 头文件
 *   │   └── src/                # 源文件
 *   ├── auth-service/           # 认证服务
 *   │   ├── include/
 *   │   └── src/
 *   ├── match-service/          # 匹配服务
 *   │   ├── include/
 *   │   └── src/
 *   └── game-service/          # 游戏服务
 *       ├── include/
 *       └── src/
 * 
 * =====================================================
 * 构建流程
 * =====================================================
 * 
 *   # 创建 build 目录
 *   mkdir build && cd build
 *   
 *   # 生成 Makefile
 *   cmake ..
 *   
 *   # 编译所有项目
 *   make
 *   
 *   # 或者只编译指定服务
 *   make auth-service
 *   make match-service
 *   make game-service
 *   
 *   # 安装
 *   make install
 * 
 *   编译产物：
 *   - build/common/libcommon.a          # 静态库
 *   - build/auth-service/auth-service   # 可执行文件
 *   - build/match-service/match-service # 可执行文件
 *   - build/game-service/game-service   # 可执行文件
 */

# =============================================================================
# 1. 基础配置
# =============================================================================

/**
 * 指定 CMake 最低版本
 * 
 * CMake 3.10 是较稳定的版本，支持大多数现代 C++ 特性
 */
cmake_minimum_required(VERSION 3.10)

/**
 * 定义项目名称和语言
 * 
 * - VERSION 1.0.0: 项目版本号
 * - LANGUAGES CXX: 使用 C++ 语言
 */
project(wuziqi2 VERSION 1.0.0 LANGUAGES CXX)

// =============================================================================
// 2. C++ 标准设置
// =============================================================================

/**
 * 设置 C++ 标准
 * 
 * C++17 特性：
 * - std::string_view（字符串视图）
 * - std::optional（可选值）
 * - 结构化绑定（auto [a, b] = ...）
 * - if constexpr
 * - std::variant
 */
set(CMAKE_CXX_STANDARD 17)

/**
 * 要求必须使用 C++17 标准
 * 
 * 如果编译器不支持 C++17，构建会失败
 */
set(CMAKE_CXX_STANDARD_REQUIRED ON)

// =============================================================================
// 3. 编译选项
# =============================================================================

/**
 * 设置编译输出目录
 * 
 * 所有可执行文件输出到 bin/ 目录
 * 所有库文件输出到 lib/ 目录
 */
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

/**
 * 编译选项
 * 
 * -Wall: 开启所有警告
 * -Wextra: 开启额外警告
 * -O2: 优化级别（代码优化）
 * -g: 包含调试信息
 */
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -g")

/**
 * 如果使用 GCC 或 Clang 编译器
 * 
 * 添加额外的编译器选项
 */
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # 添加更多警告选项
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wshadow -Wformat=2")
endif()

/**
 * 开启路径导出
 * 
 * 让 find_package 等命令能找到本项目的模块
 */
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

// =============================================================================
// 4. 查找依赖库
// =============================================================================

/**
 * 查找 OpenSSL（用于 SHA1 等加密功能）
 * 
 * 游戏服务需要 OpenSSL 进行 WebSocket 握手
 */
find_package(OpenSSL REQUIRED)

/**
 * 查找 libcurl（HTTP 客户端）
 * 
 * 服务间通信需要 HTTP 客户端
 */
find_package(CURL REQUIRED)

/**
 * 查找 MySQL 客户端库
 * 
 * 连接 MySQL 数据库需要
 */
find_package(MySQL REQUIRED)

// =============================================================================
// 5. 添加公共模块
// =============================================================================

/**
 * 添加 common 子目录
 * 
 * common 是一个静态库，被所有服务依赖
 */
add_subdirectory(common)

// =============================================================================
// 6. 添加业务服务
// =============================================================================

/**
 * 认证服务
 * 
 * 提供用户注册、登录、Token 验证等功能
 */
add_subdirectory(auth-service)

/**
 * 匹配服务
 * 
 * 提供玩家匹配、配对等功能
 */
add_subdirectory(match-service)

/**
 * 游戏服务
 * 
 * 提供游戏房间、实时对战等功能
 */
add_subdirectory(game-service)

// =============================================================================
// 7. 安装配置
# =============================================================================

/**
 * 安装目录设置
 * 
 * 默认安装到 /usr/local/
 * - 二进制文件: /usr/local/bin/
 * - 库文件: /usr/local/lib/
 */
include(GNUInstallDirs)

/**
 * 安装目标
 * 
 * 这是一个辅助目标，不实际构建任何东西
 * 只是声明安装规则
 */
install(DIRECTORY ${CMAKE_SOURCE_DIR}/auth-service/config/
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/wuziqi2
        OPTIONAL)
