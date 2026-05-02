/**
 * @file logger.cpp
 * @brief 日志系统实现
 * 
 * 单例模式的日志系统
 */

#include "logger.h"
#include <cstdarg>     // va_list, va_start, va_end
#include <iostream>    // std::cout, std::cerr
#include <chrono>      // std::chrono::system_clock
#include <iomanip>     // std::put_time

// =====================================================
// 第一部分：获取单例实例
// =====================================================

/**
 * @brief 获取单例实例
 */
Logger& Logger::instance() {
    // C++11 保证局部静态变量的线程安全性
    // 多个线程同时调用，只会初始化一次
    static Logger logger;
    return logger;
}

// =====================================================
// 第二部分：初始化
// =====================================================

/**
 * @brief 初始化日志系统
 * @param filename 日志文件名
 * @param level 日志级别
 * @param console_only 是否只输出到控制台
 */
void Logger::init(const std::string& filename, LogLevel level, bool console_only) {
    filename_ = filename;
    level_ = level;
    console_only_ = console_only;

    // 打开文件（追加模式）
    if (!filename.empty() && !console_only_) {
        // std::ios::app = append mode（追加写入）
        // std::ios::binary = binary mode（二进制模式，这里不需要）
        file_.open(filename, std::ios::app);
        
        if (!file_.is_open()) {
            // 文件打开失败，写到 stderr
            std::cerr << "无法打开日志文件: " << filename << std::endl;
        }
    }
}

// =====================================================
// 第三部分：析构函数
// =====================================================

/**
 * @brief 析构函数
 */
Logger::~Logger() {
    // 如果文件是打开的，关闭它
    if (file_.is_open()) {
        file_.flush();  // 确保所有数据都写入
        file_.close();
    }
}

// =====================================================
// 第四部分：核心输出函数
// =====================================================

/**
 * @brief 输出日志（底层）
 * @param level 日志级别
 * @param msg 日志消息
 * 
 * 流程：
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  1. 获取互斥锁（线程安全）                              │
 *   │                                                         │
 *   │  2. 格式化输出字符串                                   │
 *   │     [时间戳] [级别] 消息                                │
 *   │     例如: [2024-01-21 15:30:45] [INFO ] 连接成功        │
 *   │                                                         │
 *   │  3. 输出到控制台                                        │
 *   │     - WARN 和 ERROR 输出到 stderr（错误流）            │
 *   │     - INFO 和 DEBUG 输出到 stdout（标准输出）           │
 *   │                                                         │
 *   │  4. 输出到文件（如果有）                                │
 *   │     - 所有级别都写入文件                                │
 *   │     - 使用 flush() 确保立即写入                        │
 *   │                                                         │
 *   │  5. 释放互斥锁                                         │
 *   └─────────────────────────────────────────────────────────┘
 */
void Logger::log(LogLevel level, const std::string& msg) {
    // 检查日志级别
    // 如果 level < level_，说明这条日志不够重要，不输出
    if (level < level_) return;

    // 获取时间戳
    std::string timestamp = getTimestamp();
    
    // 获取级别字符串
    const char* level_str = levelToString(level);
    
    // 格式化的输出字符串
    std::ostringstream oss;
    oss << "[" << timestamp << "] "
        << "[" << level_str << "] "
        << msg << "\n";
    std::string output = oss.str();

    {
        // 加锁保护（线程安全）
        std::lock_guard<std::mutex> lock(mutex_);

        // 输出到控制台
        // WARN 和 ERROR 输出到 stderr
        // INFO 和 DEBUG 输出到 stdout
        if (level >= LogLevel::WARN) {
            // cerr 不经过缓冲区，直接输出
            // 错误信息用 cerr 更合适
            std::cerr << output;
        } else {
            std::cout << output;
        }

        // 输出到文件
        if (!console_only_ && file_.is_open()) {
            file_ << output;
            file_.flush();  // 立即刷盘，确保写入
        }
    }
    // 锁在这里自动释放
}

// =====================================================
// 第五部分：格式化输出
// =====================================================

/**
 * @brief 格式化字符串（类似 sprintf）
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @return 格式化后的字符串
 * 
 * 使用 va_list 处理可变参数：
 * 
 *   printf 的 %d, %s 等格式符会被正确处理
 *   LOG_INFO("用户 %s 登录，年龄 %d", name, age);
 *   变成 "用户 alice 登录，年龄 20"
 */
std::string Logger::format(const char* fmt, ...) {
    // va_list: 可变参数列表
    va_list args1, args2;
    
    // va_start: 开始遍历可变参数
    va_start(args1, fmt);
    
    // vsnprintf: 格式化字符串（类似 sprintf，但使用 va_list）
    // 第一个 INT_MAX 限制最大长度，防止缓冲区溢出
    char buffer[4096];  // 固定大小的缓冲区
    vsnprintf(buffer, sizeof(buffer), fmt, args1);
    
    // va_end: 结束遍历
    va_end(args1);
    
    return std::string(buffer);
}

// =====================================================
// 第六部分：便捷方法
// =====================================================

/**
 * @brief 输出调试信息
 */
void Logger::debug(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    log(LogLevel::DEBUG, buffer);
}

/**
 * @brief 输出一般信息
 */
void Logger::info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    log(LogLevel::INFO, buffer);
}

/**
 * @brief 输出警告信息
 */
void Logger::warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    log(LogLevel::WARN, buffer);
}

/**
 * @brief 输出错误信息
 */
void Logger::error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    log(LogLevel::ERROR, buffer);
}

// =====================================================
// 第七部分：辅助方法
// =====================================================

/**
 * @brief 获取级别字符串
 */
const char* Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        default:              return "UNKNOWN";
    }
}

/**
 * @brief 获取当前时间戳
 * @return 格式化的日期时间字符串
 * 
 * 返回格式：2024-01-21 15:30:45.123
 */
std::string Logger::getTimestamp() {
    // 获取当前时间（系统时间）
    auto now = std::chrono::system_clock::now();
    
    // 转换为 time_t（Unix 时间戳）
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    // 转换为本地时间
    struct tm local_time;
    localtime_r(&time_t_now, &local_time);  // 线程安全的版本
    
    // 格式化为字符串
    // std::ostringstream 用于构建复杂的字符串
    std::ostringstream oss;
    oss << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    
    // 添加毫秒
    // duration_cast 将秒转换为毫秒
    // % 1000 取低三位（毫秒）
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;
    
    // .stream() 是流式输出毫秒
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

// =====================================================
// 第八部分：深入理解
// =====================================================

/*
┌─────────────────────────────────────────────────────────────────┐
│                    单例模式 (Singleton Pattern)                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  单例模式的意图：                                                │
│  - 确保一个类只有一个实例                                         │
│  - 提供一个全局访问点                                             │
│                                                                 │
│  这个 Logger 类使用了最简单的单例实现：                           │
│                                                                 │
│      static Logger& instance() {                                 │
│          static Logger logger;  // 局部静态变量                   │
│          return logger;                                          │
│      }                                                           │
│                                                                 │
│  为什么用局部静态变量而不是成员变量？                             │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  1. 线程安全（C++11 保证）                               │    │
│  │     多个线程同时调用，只初始化一次                        │    │
│  │                                                         │    │
│  │  2. 惰性初始化                                           │    │
│  │     程序运行时不占用内存，第一次使用时才创建              │    │
│  │                                                         │    │
│  │  3. 自动销毁                                             │    │
│  │     程序结束时，静态局部变量会自动销毁                    │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    va_list 可变参数                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  C 风格的可变参数处理：                                          │
│                                                                 │
│  void log(const char* fmt, ...) {  // ... 表示可变参数           │
│      va_list args;                                              │
│                                                                 │
│      va_start(args, fmt);  // 开始处理，从 fmt 之后开始          │
│                                                                 │
│      // 读取参数：                                              │
│      int i = va_arg(args, int);  // 读取一个 int                │
│      const char* s = va_arg(args, const char*);  // 读取指针    │
│                                                                 │
│      va_end(args);  // 结束处理                                  │
│  }                                                              │
│                                                                 │
│  vsnprintf 的用法：                                             │
│                                                                 │
│      char buf[1024];                                            │
│      vsnprintf(buf, sizeof(buf), fmt, args);                   │
│                                                                 │
│      // 等价于：                                                │
│      // printf(fmt, ...);  但结果存到 buf 而不是 stdout         │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    stderr vs stdout                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  stdout（标准输出）：                                            │
│  - 正常的程序输出                                               │
│  - 默认是缓冲的（行缓冲或全缓冲）                                 │
│  - 可以重定向到文件：./program > output.txt                      │
│                                                                 │
│  stderr（标准错误）：                                            │
│  - 错误信息输出                                                 │
│  - 不缓冲，直接输出                                             │
│  - 可以单独重定向：./program 2> error.txt                        │
│                                                                 │
│  常用技巧：                                                     │
│      ./program > output.txt 2> errors.txt  # 分别重定向           │
│      ./program > all.txt 2>&1            # stderr 合并到 stdout  │
│      ./program > /dev/null 2>&1         # 丢弃所有输出           │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
*/

// =====================================================
// 使用示例（注释）
// =====================================================
/*
// 示例 1：基本使用
void example1() {
    // 初始化（可选，有默认值）
    Logger::instance().init("app.log", LogLevel::INFO);
    
    // 使用宏输出日志
    LOG_DEBUG("调试信息: x = %d", 10);
    LOG_INFO("程序启动，用户数: %d", 100);
    LOG_WARN("内存使用率: %d%%", 85);
    LOG_ERROR("数据库连接失败: %s", "timeout");
}

// 示例 2：不同级别的过滤
void example2() {
    // 设置只输出 ERROR
    Logger::instance().setLevel(LogLevel::ERROR);
    
    // 只有 ERROR 会被输出
    LOG_DEBUG("这行不会输出");  // DEBUG < ERROR
    LOG_INFO("这行不会输出");   // INFO < ERROR
    LOG_WARN("这行不会输出");   // WARN < ERROR
    LOG_ERROR("这行会输出");    // ERROR >= ERROR
}

// 示例 3：格式化各种类型
void example3() {
    int i = 42;
    double f = 3.14159;
    const char* s = "hello";
    std::string ss = "world";
    
    LOG_INFO("整数: %d, 浮点: %.2f, 字符: %s", i, f, s);
    LOG_INFO("std::string: %s", ss.c_str());  // 需要 .c_str()
    LOG_INFO("十六进制: 0x%x, 地址: %p", i, &i);
    LOG_INFO("布尔: %d (0=false, 非0=true)", true);
}

// 示例 4：输出到不同位置
void example4() {
    // 只输出到控制台
    Logger::instance().init("", LogLevel::DEBUG, true);
    
    // 输出到文件和标准输出
    Logger::instance().init("app.log", LogLevel::DEBUG, false);
    
    // 输出到标准错误
    // stderr 不会被缓冲，适合紧急信息
    std::cerr << "紧急信息" << std::endl;
}

// 示例 5：自定义日志格式
void example5() {
    // 这个 Logger 的格式是固定的
    // 如果需要自定义格式，可以继承并重写 log() 方法
    
    // 例如添加线程 ID：
    // oss << "[线程ID:" << std::this_thread::get_id() << "] ";
    // oss << "[" << timestamp << "] ";
    // oss << "[" << level_str << "] ";
    // oss << msg << "\n";
}

// 示例 6：运行时改变日志级别
void example6() {
    // 程序启动时设置
    Logger::instance().init("app.log", LogLevel::INFO);
    
    // 收到 SIGHUP 信号时重新加载配置
    // 可以动态改变日志级别
    void handle_sighup() {
        Logger::instance().setLevel(LogLevel::DEBUG);
        LOG_INFO("日志级别已更改为 DEBUG");
    }
}
*/
