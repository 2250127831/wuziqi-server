/**
 * @file logger.h
 * @brief 日志系统封装
 * 
 * =====================================================
 * 什么是日志？
 * =====================================================
 * 
 * 日志是程序运行时记录的信息：
 * - 调试信息：帮助开发者排查问题
 * - 运行状态：记录程序在做什么
 * - 错误信息：记录出了什么问题
 * 
 * 为什么要用日志而不是 print？
 * - 日志有级别（DEBUG/INFO/WARN/ERROR）
 * - 可以输出到不同地方（控制台/文件）
 * - 可以控制哪些信息输出
 * - 格式更规范
 * 
 * =====================================================
 * 日志级别
 * =====================================================
 * 
 * ┌─────────────────────────────────────────────────────────────┐
 * │                                                             │
 * │   DEBUG  ← 最低级别，最详细                                 │
 * │   INFO   ← 一般信息                                         │
 * │   WARN   ← 警告（不影响运行）                               │
 * │   ERROR  ← 错误（需要关注）                                 │
 * │                                                             │
 * │   设置级别 = 输出 >= 该级别的日志                            │
 * │   例如：设置 INFO，会输出 INFO/WARN/ERROR                    │
 * │        设置 ERROR，只会输出 ERROR                            │
 * │                                                             │
 * └─────────────────────────────────────────────────────────────┘
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <string>          // std::string
#include <mutex>          // std::mutex
#include <fstream>        // std::ofstream
#include <sstream>         // std::ostringstream
#include <ctime>         // time_t, struct tm
#include <iomanip>       // std::put_time

// =====================================================
// 第一部分：日志级别
// =====================================================
/**
 * @enum LogLevel
 * @brief 日志级别枚举
 * 
 * 级别从低到高：
 * - DEBUG: 开发调试信息，最详细
 * - INFO: 一般运行信息
 * - WARN: 警告信息
 * - ERROR: 错误信息
 * 
 * 设置日志级别后，只会输出 >= 该级别的日志
 * 例如：设置为 INFO，只会输出 INFO/WARN/ERROR
 */
enum class LogLevel {
    DEBUG = 0,  // 调试信息
    INFO = 1,   // 一般信息
    WARN = 2,   // 警告
    ERROR = 3,  // 错误
};

// =====================================================
// 第二部分：Logger 类
// =====================================================
/**
 * @class Logger
 * @brief 日志系统（单例模式）
 * 
 * 设计模式：单例模式
 * - 整个程序只有一个 Logger 实例
 * - 通过 Logger::instance() 获取
 * 
 * 输出格式：
 *   [时间戳] [级别] 消息
 * 
 * 例如：
 *   [2024-01-21 15:30:45.123] [INFO ] 连接池初始化成功
 *   [2024-01-21 15:30:46.789] [ERROR] MySQL 查询失败: 连接超时
 * 
 * 输出目标：
 * - 控制台：DEBUG/WARN/ERROR 输出到 stderr，INFO 输出到 stdout
 * - 文件：所有日志写入文件（追加模式）
 */
class Logger {
public:
    /**
     * @brief 获取单例实例
     * @return Logger 的唯一实例
     * 
     * 使用局部静态变量实现单例：
     * - 线程安全（C++11 保证）
     * - 惰性初始化（第一次使用时才创建）
     * - 不用担心内存泄漏（程序结束时自动销毁）
     */
    static Logger& instance();

    /**
     * @brief 初始化日志系统
     * @param filename 日志文件名（如果为空，只输出到控制台）
     * @param level 日志级别
     * @param console_only 是否只输出到控制台
     */
    void init(const std::string& filename = "", LogLevel level = LogLevel::INFO,
              bool console_only = false);

    // =====================================================
    // 日志输出接口
    // =====================================================
    
    /**
     * @brief 输出调试信息
     * @param fmt 格式化字符串（类似 printf）
     * @param ... 可变参数
     * 
     * 支持 printf 风格的格式化：
     *   LOG_DEBUG("用户 %s 登录", username.c_str());
     *   LOG_DEBUG("数值: %d, 浮点: %.2f", i, f);
     */
    void debug(const char* fmt, ...);
    
    /**
     * @brief 输出一般信息
     */
    void info(const char* fmt, ...);
    
    /**
     * @brief 输出警告信息
     */
    void warn(const char* fmt, ...);
    
    /**
     * @brief 输出错误信息
     */
    void error(const char* fmt, ...);

    /**
     * @brief 输出日志（底层接口）
     * @param level 日志级别
     * @param msg 日志消息
     */
    void log(LogLevel level, const std::string& msg);

    /**
     * @brief 格式化字符串（类似 sprintf）
     * @param fmt 格式化字符串
     * @param ... 可变参数
     * @return 格式化后的字符串
     */
    static std::string format(const char* fmt, ...);

    /**
     * @brief 获取级别字符串
     */
    static const char* levelToString(LogLevel level);

    /**
     * @brief 获取当前时间戳字符串
     * @return 格式化的日期时间字符串
     * 
     * 返回格式：2024-01-21 15:30:45.123
     */
    static std::string getTimestamp();

    /**
     * @brief 获取当前日志级别
     */
    LogLevel getLevel() const { return level_; }

    /**
     * @brief 设置日志级别
     */
    void setLevel(LogLevel level) { level_ = level; }

// =====================================================
// 私有成员
// =====================================================
private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    Logger() = default;
    
    /**
     * @brief 私有析构函数
     */
    ~Logger();

    /**
     * @brief 私有拷贝构造函数（禁止拷贝）
     */
    Logger(const Logger&) = delete;
    
    /**
     * @brief 私有赋值运算符（禁止赋值）
     */
    Logger& operator=(const Logger&) = delete;

    // ==================== 成员变量 ====================
    
    std::string filename_;           // 日志文件名
    std::ofstream file_;             // 日志文件流
    LogLevel level_ = LogLevel::INFO; // 日志级别
    bool console_only_ = false;       // 是否只输出到控制台
    mutable std::mutex mutex_;        // 互斥锁（保护文件写入）
};

// =====================================================
// 第三部分：便捷宏定义
// =====================================================
/**
 * @def LOG_DEBUG(fmt, ...)
 * @brief 输出调试日志
 * 
 * 使用示例：
 *   LOG_DEBUG("变量 x = %d", x);
 */
#define LOG_DEBUG(fmt, ...) Logger::instance().debug(fmt, ##__VA_ARGS__)

/**
 * @def LOG_INFO(fmt, ...)
 * @brief 输出信息日志
 */
#define LOG_INFO(fmt, ...) Logger::instance().info(fmt, ##__VA_ARGS__)

/**
 * @def LOG_WARN(fmt, ...)
 * @brief 输出警告日志
 */
#define LOG_WARN(fmt, ...) Logger::instance().warn(fmt, ##__VA_ARGS__)

/**
 * @def LOG_ERROR(fmt, ...)
 * @brief 输出错误日志
 */
#define LOG_ERROR(fmt, ...) Logger::instance().error(fmt, ##__VA_ARGS__)

/**
 * @def LOG(level, fmt, ...)
 * @brief 输出指定级别的日志
 */
#define LOG(level, fmt, ...) Logger::instance().log(level, Logger::format(fmt, ##__VA_ARGS__))

#endif // LOGGER_H
