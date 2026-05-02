/**
 * @file main.cpp
 * @brief 匹配服务入口
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
 *   │      -r, --redis: Redis 地址                          │
 *   │      -g, --game-url: 游戏服务地址                      │
 *   │                                                         │
 *   │   2. 初始化日志                                        │
 *   │                                                         │
 *   │   3. 初始化 Redis 连接池                               │
 *   │                                                         │
 *   │   4. 初始化 Handler                                     │
 *   │                                                         │
 *   │   5. 启动 HTTP 服务器                                  │
 *   │                                                         │
 *   │   6. 主循环                                            │
 *   │      - 处理请求                                        │
 *   │      - 定时任务（清理过期匹配）                         │
 *   │                                                         │
 *   └─────────────────────────────────────────────────────────┘
 */

#include <iostream>
#include <string>
#include <signal.h>
#include <getopt.h>

#include "handler.h"
#include "logger.h"
#include "redis_pool.h"
#include "thread_pool.h"
#include "http_server.h"

// 全局变量
static bool g_running = true;

void signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        LOG_INFO("收到信号 %d，准备退出...", sig);
        g_running = false;
    }
}

void printUsage(const char* program_name) {
    std::cout << "用法: " << program_name << " [选项]\n";
    std::cout << "\n选项:\n";
    std::cout << "  -p, --port <端口>        HTTP 监听端口 (默认: 8081)\n";
    std::cout << "  -r, --redis <地址>       Redis 地址 (默认: 127.0.0.1:6379)\n";
    std::cout << "  -g, --game-url <地址>    游戏服务地址 (默认: http://127.0.0.1:8082)\n";
    std::cout << "  -l, --log <日志文件>     日志文件 (默认: match.log)\n";
    std::cout << "  -v, --verbose            详细输出\n";
    std::cout << "  -h, --help               显示帮助\n";
}

int main(int argc, char* argv[]) {
    // ==================== 1. 解析命令行参数 ====================
    
    int port = 8081;                          // HTTP 端口
    std::string redis_addr = "127.0.0.1:6379"; // Redis 地址
    std::string game_service_url = "http://127.0.0.1:8082";  // 游戏服务地址
    std::string log_file = "match.log";
    bool verbose = false;

    static struct option long_options[] = {
        {"port",        required_argument, 0, 'p'},
        {"redis",       required_argument, 0, 'r'},
        {"game-url",    required_argument, 0, 'g'},
        {"log",         required_argument, 0, 'l'},
        {"verbose",     no_argument,       0, 'v'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "p:r:g:l:vh", 
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'p': port = std::stoi(optarg); break;
            case 'r': redis_addr = optarg; break;
            case 'g': game_service_url = optarg; break;
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
    LOG_INFO("match-service 启动中...");
    LOG_INFO("监听端口: %d", port);
    LOG_INFO("Redis: %s", redis_addr.c_str());
    LOG_INFO("游戏服务: %s", game_service_url.c_str());
    LOG_INFO("=================================================");

    // ==================== 3. 注册信号处理 ====================
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // ==================== 4. 解析 Redis 地址 ====================
    
    std::string redis_host = redis_addr.substr(0, redis_addr.find(':'));
    int redis_port = std::stoi(redis_addr.substr(redis_addr.find(':') + 1));

    // ==================== 5. 初始化 Redis 连接池 ====================
    
    redis_pool_config redis_config;
    redis_config.host = redis_host;
    redis_config.port = redis_port;
    redis_config.pool_size = 8;
    
    auto redis_pool = std::make_shared<RedisPool>(redis_config);
    if (!redis_pool->init()) {
        LOG_ERROR("Redis 连接池初始化失败");
        return 1;
    }
    LOG_INFO("Redis 连接池初始化成功");

    // ==================== 6. 初始化线程池 ====================
    
    auto thread_pool = std::make_shared<ThreadPool>(4);

    // ==================== 7. 初始化 Handler ====================
    
    auto handler = std::make_shared<MatchServiceHandler>(
        redis_pool, thread_pool, game_service_url);
    
    if (!handler->init()) {
        LOG_ERROR("MatchServiceHandler 初始化失败");
        return 1;
    }
    LOG_INFO("MatchServiceHandler 初始化成功");

    // ==================== 8. 启动 HTTP 服务器 ====================
    
    HttpServer server(port);
    
    // 注册路由
    // POST /api/match - 加入匹配
    server.registerHandler("/api/match", [handler](const HttpRequest& req) {
        int user_id = std::stoi(req.getParam("user_id"));
        std::string username = req.getParam("username");
        int level = std::stoi(req.getParam("level"));
        
        std::string response = handler->handleJoinMatch(user_id, username, level);
        
        return HttpResponse(200, response, {{"Content-Type", "application/json"}});
    });

    // DELETE /api/match - 离开匹配
    server.registerHandler("/api/match/leave", [handler](const HttpRequest& req) {
        int user_id = std::stoi(req.getParam("user_id"));
        
        std::string response = handler->handleLeaveMatch(user_id);
        
        return HttpResponse(200, response, {{"Content-Type", "application/json"}});
    });

    // GET /api/match/status - 查询匹配状态
    server.registerHandler("/api/match/status", [handler](const HttpRequest& req) {
        int user_id = std::stoi(req.getParam("user_id"));
        
        std::string response = handler->handleGetMatchStatus(user_id);
        
        return HttpResponse(200, response, {{"Content-Type", "application/json"}});
    });

    // GET /api/match/result - 获取匹配结果
    server.registerHandler("/api/match/result", [handler](const HttpRequest& req) {
        int user_id = std::stoi(req.getParam("user_id"));
        
        std::string response = handler->handleGetMatchResult(user_id);
        
        return HttpResponse(200, response, {{"Content-Type", "application/json"}});
    });

    // 健康检查
    server.registerHandler("/health", [](const HttpRequest& req) {
        return HttpResponse(200, R"({"status":"ok","service":"match"})", 
                           {{"Content-Type", "application/json"}});
    });

    // 启动服务器
    if (!server.start()) {
        LOG_ERROR("HTTP 服务器启动失败");
        return 1;
    }

    LOG_INFO("match-service 已启动，监听端口: %d", port);

    // ==================== 9. 主循环 ====================
    
    long long last_cleanup_time = Utils::getCurrentTimestamp();
    
    while (g_running) {
        // 处理请求
        server.poll(100);
        
        // 定时任务：每 60 秒清理过期匹配
        long long now = Utils::getCurrentTimestamp();
        if (now - last_cleanup_time >= 60) {
            // TODO: 清理超时的匹配
            last_cleanup_time = now;
        }
    }

    // ==================== 10. 清理 ====================
    
    LOG_INFO("match-service 关闭中...");
    
    server.stop();
    thread_pool->shutdown();
    
    LOG_INFO("match-service 已关闭");
    return 0;
}
