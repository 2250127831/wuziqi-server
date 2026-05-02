/**
 * @file main.cpp
 * @brief 认证服务入口
 * 
 * =====================================================
 * 整体架构
 * =====================================================
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │                   main() 入口                            │
 *   ├─────────────────────────────────────────────────────────┤
 *   │                                                         │
 *   │   1. 解析命令行参数                                     │
 *   │      -p, --port: HTTP 监听端口                         │
 *   │      -c, --config: 配置文件路径                        │
 *   │                                                         │
 *   │   2. 初始化日志系统                                     │
 *   │      Logger::instance().init("auth.log", ...)          │
 *   │                                                         │
 *   │   3. 初始化数据库连接                                   │
 *   │      - MySQL 连接池                                    │
 *   │      - Redis 连接池                                    │
 *   │                                                         │
 *   │   4. 初始化 Handler                                     │
 *   │      - 创建用户表                                      │
 *   │                                                         │
 *   │   5. 启动 HTTP 服务器                                  │
 *   │      - 监听端口                                        │
 *   │      - 注册路由                                        │
 *   │                                                         │
 *   │   6. 主循环                                            │
 *   │      - 处理请求                                        │
 *   │      - 日志输出                                        │
 *   │                                                         │
 *   └─────────────────────────────────────────────────────────┘
 */

#include <iostream>     // std::cout, std::cerr
#include <string>      // std::string
#include <signal.h>    // signal, SIGINT, SIGTERM
#include <getopt.h>    // getopt_long, struct option
#include <unistd.h>    // getopt

#include "handler.h"
#include "logger.h"
#include "redis_pool.h"
#include "mysql_pool.h"
#include "thread_pool.h"

// 引入 HTTP 服务器（这里用简化的实现）
#include "http_server.h"

// =====================================================
// 全局变量
// =====================================================

/** @brief 服务是否在运行 */
static bool g_running = true;

/**
 * @brief 信号处理
 * 
 * 当收到 SIGINT（Ctrl+C）或 SIGTERM（kill）时优雅退出
 */
void signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        LOG_INFO("收到信号 %d，准备退出...", sig);
        g_running = false;
    }
}

// =====================================================
// 打印使用方法
// =====================================================

/**
 * @brief 打印使用方法
 */
void printUsage(const char* program_name) {
    std::cout << "用法: " << program_name << " [选项]\n";
    std::cout << "\n选项:\n";
    std::cout << "  -p, --port <端口>        HTTP 监听端口 (默认: 8080)\n";
    std::cout << "  -r, --redis <地址>      Redis 地址 (默认: 127.0.0.1:6379)\n";
    std::cout << "  -m, --mysql <地址>      MySQL 地址 (默认: 127.0.0.1:3306)\n";
    std::cout << "  -u, --mysql-user <用户>  MySQL 用户 (默认: root)\n";
    std::cout << "  -w, --mysql-pass <密码>  MySQL 密码 (默认: 空)\n";
    std::cout << "  -d, --database <数据库>  MySQL 数据库 (默认: auth_db)\n";
    std::cout << "  -l, --log <日志文件>     日志文件 (默认: auth.log)\n";
    std::cout << "  -v, --verbose           详细输出\n";
    std::cout << "  -h, --help              显示帮助\n";
    std::cout << "\n示例:\n";
    std::cout << "  " << program_name << " -p 8080 -r 127.0.0.1:6379\n";
    std::cout << "  " << program_name << " --port=9000 --redis=10.0.0.1:6379\n";
}

// =====================================================
// 主函数
// =====================================================

int main(int argc, char* argv[]) {
    // ==================== 1. 解析命令行参数 ====================
    
    int port = 8080;                    // HTTP 端口
    std::string redis_addr = "127.0.0.1:6379";
    std::string mysql_addr = "127.0.0.1:3306";
    std::string mysql_user = "root";
    std::string mysql_pass = "";
    std::string mysql_db = "auth_db";
    std::string log_file = "auth.log";
    bool verbose = false;

    // 命令行参数定义
    static struct option long_options[] = {
        {"port",        required_argument, 0, 'p'},
        {"redis",       required_argument, 0, 'r'},
        {"mysql",       required_argument, 0, 'm'},
        {"mysql-user",  required_argument, 0, 'u'},
        {"mysql-pass",  required_argument, 0, 'w'},
        {"database",    required_argument, 0, 'd'},
        {"log",         required_argument, 0, 'l'},
        {"verbose",     no_argument,       0, 'v'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "p:r:m:u:w:d:l:vh", 
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'p': port = std::stoi(optarg); break;
            case 'r': redis_addr = optarg; break;
            case 'm': mysql_addr = optarg; break;
            case 'u': mysql_user = optarg; break;
            case 'w': mysql_pass = optarg; break;
            case 'd': mysql_db = optarg; break;
            case 'l': log_file = optarg; break;
            case 'v': verbose = true; break;
            case 'h': 
                printUsage(argv[0]);
                return 0;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }

    // ==================== 2. 初始化日志 ====================
    
    Logger::instance().init(log_file, verbose ? LogLevel::DEBUG : LogLevel::INFO);
    LOG_INFO("=================================================");
    LOG_INFO("auth-service 启动中...");
    LOG_INFO("监听端口: %d", port);
    LOG_INFO("Redis: %s", redis_addr.c_str());
    LOG_INFO("MySQL: %s/%s", mysql_addr.c_str(), mysql_db.c_str());
    LOG_INFO("=================================================");

    // ==================== 3. 注册信号处理 ====================
    
    signal(SIGINT, signalHandler);   // Ctrl+C
    signal(SIGTERM, signalHandler);  // kill

    // ==================== 4. 解析 Redis 地址 ====================
    
    // redis_addr 格式: "127.0.0.1:6379"
    std::string redis_host = redis_addr.substr(0, redis_addr.find(':'));
    int redis_port = std::stoi(redis_addr.substr(redis_addr.find(':') + 1));

    // ==================== 5. 解析 MySQL 地址 ====================
    
    std::string mysql_host = mysql_addr.substr(0, mysql_addr.find(':'));
    int mysql_port = std::stoi(mysql_addr.substr(mysql_addr.find(':') + 1));

    // ==================== 6. 初始化 Redis 连接池 ====================
    
    redis_pool_config redis_config;
    redis_config.host = redis_host;
    redis_config.port = redis_port;
    redis_config.password = "";  // 如果有密码
    redis_config.pool_size = 8;
    
    auto redis_pool = std::make_shared<RedisPool>(redis_config);
    if (!redis_pool->init()) {
        LOG_ERROR("Redis 连接池初始化失败");
        return 1;
    }
    LOG_INFO("Redis 连接池初始化成功");

    // ==================== 7. 初始化 MySQL 连接池 ====================
    
    mysql_pool_config mysql_config;
    mysql_config.host = mysql_host;
    mysql_config.port = mysql_port;
    mysql_config.user = mysql_user;
    mysql_config.password = mysql_pass;
    mysql_config.database = mysql_db;
    mysql_config.pool_size = 8;
    
    auto mysql_pool = std::make_shared<MySqlPool>(mysql_config);
    if (!mysql_pool->init()) {
        LOG_ERROR("MySQL 连接池初始化失败");
        return 1;
    }
    LOG_INFO("MySQL 连接池初始化成功");

    // ==================== 8. 初始化线程池 ====================
    
    auto thread_pool = std::make_shared<ThreadPool>(4);
    LOG_INFO("线程池初始化成功");

    // ==================== 9. 初始化 Handler ====================
    
    auto handler = std::make_shared<AuthServiceHandler>(redis_pool, mysql_pool, thread_pool);
    if (!handler->init()) {
        LOG_ERROR("AuthServiceHandler 初始化失败");
        return 1;
    }

    // ==================== 10. 启动 HTTP 服务器 ====================
    
    HttpServer server(port);
    
    // 注册路由
    server.registerHandler("/api/register", [handler](const HttpRequest& req) {
        // 获取参数
        std::string username = req.getParam("username");
        std::string password = req.getParam("password");
        
        // 处理请求
        std::string response = handler->handleRegister(username, password);
        
        // 返回 JSON
        return HttpResponse(200, response, {{"Content-Type", "application/json"}});
    });

    server.registerHandler("/api/login", [handler](const HttpRequest& req) {
        std::string username = req.getParam("username");
        std::string password = req.getParam("password");
        
        std::string response = handler->handleLogin(username, password);
        
        return HttpResponse(200, response, {{"Content-Type", "application/json"}});
    });

    server.registerHandler("/api/verify", [handler](const HttpRequest& req) {
        std::string token = req.getParam("token");
        
        std::string response = handler->handleVerifyToken(token);
        
        return HttpResponse(200, response, {{"Content-Type", "application/json"}});
    });

    server.registerHandler("/api/refresh", [handler](const HttpRequest& req) {
        std::string old_token = req.getParam("old_token");
        
        std::string response = handler->handleRefreshToken(old_token);
        
        return HttpResponse(200, response, {{"Content-Type", "application/json"}});
    });

    // 用户信息查询
    server.registerHandler("/api/user/:id", [handler](const HttpRequest& req) {
        int user_id = std::stoi(req.getParam("id"));
        
        std::string response = handler->handleGetUserInfo(user_id);
        
        return HttpResponse(200, response, {{"Content-Type", "application/json"}});
    });

    // 健康检查
    server.registerHandler("/health", [](const HttpRequest& req) {
        return HttpResponse(200, R"({"status":"ok"})", {{"Content-Type", "application/json"}});
    });

    // 启动服务器
    if (!server.start()) {
        LOG_ERROR("HTTP 服务器启动失败");
        return 1;
    }

    LOG_INFO("auth-service 已启动，监听端口: %d", port);

    // ==================== 11. 主循环 ====================
    
    while (g_running) {
        // 处理请求
        server.poll(100);  // 100ms 超时
        
        // 可以在这里添加定时任务
        // 比如：
        // - 清理过期的 Token
        // - 更新统计数据
        // - 检查连接池状态
    }

    // ==================== 12. 清理 ====================
    
    LOG_INFO("auth-service 关闭中...");
    
    // 清理顺序很重要！
    // 1. 先停止接收新请求
    server.stop();
    
    // 2. 再关闭线程池（等待所有任务完成）
    thread_pool->shutdown();
    
    // 3. 最后关闭连接池
    // （自动析构）

    LOG_INFO("auth-service 已关闭");
    return 0;
}

// =====================================================
// 启动命令示例
// =====================================================
/*
# 基本启动
./auth-service

# 指定端口
./auth-service -p 9000

# 指定 Redis 和 MySQL
./auth-service -r 10.0.0.1:6379 -m 10.0.0.2:3306 -u admin -w secret -d auth_db

# 详细日志
./auth-service -v -l auth_debug.log

# Docker 部署
docker run -p 8080:8080 \
  -e REDIS_ADDR=redis:6379 \
  -e MYSQL_ADDR=mysql:3306 \
  -e MYSQL_USER=root \
  -e MYSQL_PASS=secret \
  auth-service
*/
