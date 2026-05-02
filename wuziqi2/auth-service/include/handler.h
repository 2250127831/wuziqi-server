/**
 * @file handler.h
 * @brief 认证服务处理器
 * 
 * =====================================================
 * 什么是认证服务？
 * =====================================================
 * 
 * 认证服务负责：
 * 1. 用户注册 - 创建新账号
 * 2. 用户登录 - 验证身份，发放 Token
 * 3. Token 验证 - 验证其他服务的 Token
 * 4. 用户信息 - 获取用户详情
 * 
 * =====================================================
 * 整体架构
 * =====================================================
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │                   Auth Service                          │
 *   ├─────────────────────────────────────────────────────────┤
 *   │                                                         │
 *   │   HTTP API:                                            │
 *   │   ├── POST /api/register  ← 用户注册                   │
 *   │   ├── POST /api/login     ← 用户登录                   │
 *   │   ├── POST /api/verify    ← 验证 Token                  │
 *   │   ├── POST /api/refresh   ← 刷新 Token                  │
 *   │   └── GET  /api/user/:id  ← 获取用户信息               │
 *   │                                                         │
 *   │   数据存储:                                            │
 *   │   ├── MySQL: 用户表（用户名、密码哈希）                 │
 *   │   └── Redis: 会话（Token → UserID）                    │
 *   │                                                         │
 *   │   Token 机制:                                          │
 *   │   ├── JWT 格式                                         │
 *   │   ├── 7 天有效期                                       │
 *   │   └── 包含用户 ID                                      │
 *   │                                                         │
 *   └─────────────────────────────────────────────────────────┘
 */

#ifndef AUTH_SERVICE_HANDLER_H
#define AUTH_SERVICE_HANDLER_H

#include <string>          // std::string
#include <memory>         // std::shared_ptr
#include <functional>      // std::function
#include <map>           // std::map

// 引入公共模块
#include "redis_pool.h"   // Redis 连接池
#include "mysql_pool.h"   // MySQL 连接池
#include "thread_pool.h"  // 线程池
#include "logger.h"       // 日志系统
#include "utils.h"        // 工具函数

/**
 * @class AuthServiceHandler
 * @brief 认证服务处理器
 * 
 * 处理用户注册、登录、Token 验证等功能
 * 
 * 数据流：
 * 
 *   ┌──────────┐     ┌──────────────────┐     ┌─────────┐
 *   │ HTTP     │ ──► │ AuthService      │ ──► │ MySQL   │
 *   │ Request  │     │ Handler          │     │ (用户)  │
 *   └──────────┘     └──────────────────┘     └─────────┘
 *                            │
 *                            ▼
 *                       ┌─────────┐
 *                       │ Redis   │
 *                       │ (会话)  │
 *                       └─────────┘
 */
class AuthServiceHandler {
public:
    /**
     * @brief 构造函数
     * @param redis Redis 连接池（用于存储会话）
     * @param mysql MySQL 连接池（用于存储用户数据）
     * @param pool 线程池（用于异步任务）
     */
    AuthServiceHandler(std::shared_ptr<RedisPool> redis,
                       std::shared_ptr<MySqlPool> mysql,
                       std::shared_ptr<ThreadPool> pool);
    
    /**
     * @brief 析构函数
     */
    ~AuthServiceHandler() = default;

    // =====================================================
    // 初始化
    // =====================================================
    /**
     * @brief 初始化认证服务
     * @return true 成功
     * 
     * 主要工作：
     * 1. 创建用户表（如果不存在）
     * 2. 初始化完成
     */
    bool init();

    // =====================================================
    // HTTP 请求处理（返回 JSON 字符串）
    // =====================================================
    
    /**
     * @brief 处理用户注册
     * @param username 用户名
     * @param password 密码
     * @return JSON 响应字符串
     * 
     * 流程：
     * 
     *   ┌─────────────────────────────────────────────────┐
     *   │  1. 验证参数                                    │
     *   │     - 用户名: 3-50 字符                         │
     *   │     - 密码: 至少 6 字符                         │
     *   │                                                 │
     *   │  2. 检查用户名是否已存在                         │
     *   │     SELECT id FROM users WHERE username = ?    │
     *   │                                                 │
     *   │  3. 创建用户                                    │
     *   │     INSERT INTO users (username, password_hash) │
     *   │                                                 │
     *   │  4. 生成 Token                                  │
     *   │     Utils::generateToken()                      │
     *   │                                                 │
     *   │  5. 存储会话到 Redis                            │
     *   │     SET session:user_id token                  │
     *   │                                                 │
     *   │  6. 返回结果                                    │
     *   │     {user_id, username, token}                  │
     *   └─────────────────────────────────────────────────┘
     */
    std::string handleRegister(const std::string& username, const std::string& password);
    
    /**
     * @brief 处理用户登录
     * @param username 用户名
     * @param password 密码
     * @return JSON 响应字符串
     * 
     * 流程：
     * 
     *   ┌─────────────────────────────────────────────────┐
     *   │  1. 验证参数                                    │
     *   │                                                 │
     *   │  2. 查询用户                                    │
     *   │     SELECT id, password_hash FROM users        │
     *   │     WHERE username = ?                          │
     *   │                                                 │
     *   │  3. 验证密码                                    │
     *   │     input_hash == stored_hash                   │
     *   │                                                 │
     *   │  4. 生成新 Token                                │
     *   │                                                 │
     *   │  5. 更新 Redis 会话                             │
     *   │                                                 │
     *   │  6. 返回结果                                    │
     *   └─────────────────────────────────────────────────┘
     */
    std::string handleLogin(const std::string& username, const std::string& password);
    
    /**
     * @brief 验证 Token
     * @param token 要验证的 Token
     * @return JSON 响应字符串
     * 
     * 其他服务调用此接口验证用户身份
     */
    std::string handleVerifyToken(const std::string& token);
    
    /**
     * @brief 获取用户信息
     * @param user_id 用户 ID
     * @return JSON 响应字符串
     * 
     * 先查 Redis 缓存，没有再查 MySQL
     */
    std::string handleGetUserInfo(int user_id);
    
    /**
     * @brief 刷新 Token
     * @param old_token 旧 Token
     * @return JSON 响应字符串
     * 
     * 验证旧 Token，有效则生成新 Token
     */
    std::string handleRefreshToken(const std::string& old_token);

// =====================================================
// 私有成员
// =====================================================
private:
    /**
     * @brief 缓存用户信息到 Redis
     * @param user_id 用户 ID
     * @param user 用户信息（键值对）
     * 
     * Redis Key 格式：user:info:{user_id}
     * 过期时间：1 小时
     */
    void cacheUserInfo(int user_id, const std::map<std::string, std::string>& user);

    /**
     * @brief 获取用户信息（优先缓存）
     * @param user_id 用户 ID
     * @return 用户信息
     * 
     * 流程：
     * 1. 先查 Redis 缓存
     * 2. 没有则查 MySQL
     * 3. 存入缓存
     */
    std::map<std::string, std::string> getUserInfo(int user_id);

    // ==================== 成员变量 ====================
    
    std::shared_ptr<RedisPool> redis_;      // Redis 连接池
    std::shared_ptr<MySqlPool> mysql_;      // MySQL 连接池
    std::shared_ptr<ThreadPool> thread_pool_; // 线程池

    // ==================== 配置常量 ====================
    
    /** @brief Token 签名密钥 */
    const std::string secret_ = "wuziqi-auth-secret-2024";
    
    /** @brief Token 有效期：7 天 */
    const int token_expire_seconds_ = 3600 * 24 * 7;
    
    /** @brief 用户缓存过期时间：1 小时 */
    const int user_cache_ttl_ = 3600;
};

#endif // AUTH_SERVICE_HANDLER_H
