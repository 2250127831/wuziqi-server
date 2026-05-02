/**
 * @file handler.cpp
 * @brief 认证服务处理器实现
 */

#include "handler.h"

// =====================================================
// 第一部分：构造和初始化
// =====================================================

/**
 * @brief 构造函数
 */
AuthServiceHandler::AuthServiceHandler(std::shared_ptr<RedisPool> redis,
                                       std::shared_ptr<MySqlPool> mysql,
                                       std::shared_ptr<ThreadPool> pool)
    : redis_(redis)
    , mysql_(mysql)
    , thread_pool_(pool) {
}

/**
 * @brief 初始化
 */
bool AuthServiceHandler::init() {
    LOG_INFO("auth-service 初始化开始");

    // 创建用户表（如果不存在）
    // IF NOT EXISTS 防止重复创建报错
    auto guard = mysql_->getConn();
    if (!guard->execute(
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  username VARCHAR(50) UNIQUE NOT NULL,"
        "  password_hash VARCHAR(255) NOT NULL,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_username (username)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    )) {
        LOG_ERROR("创建用户表失败: %s", guard->getError().c_str());
        return false;
    }

    LOG_INFO("auth-service 初始化完成");
    return true;
}

// =====================================================
// 第二部分：用户注册
// =====================================================

/**
 * @brief 处理用户注册
 * 
 * 详细流程：
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  1. 验证输入                                            │
 *   │     - 用户名长度: 3-50                                  │
 *   │     - 密码长度: >= 6                                    │
 *   │                                                         │
 *   │  2. 查数据库，检查用户名是否存在                           │
 *   │     SELECT id FROM users WHERE username = ?             │
 *   │                                                         │
 *   │  3. 密码哈希                                            │
 *   │     - 使用 Utils::sha256()                              │
 *   │     - 密码不存储明文！                                   │
 *   │                                                         │
 *   │  4. 插入数据库                                          │
 *   │     INSERT INTO users (username, password_hash)        │
 *   │                                                         │
 *   │  5. 生成 Token                                          │
 *   │     Utils::generateToken(user_id, secret)              │
 *   │                                                         │
 *   │  6. 存储会话（Redis）                                    │
 *   │     session:{user_id} = token                          │
 *   │     有效期 7 天                                         │
 *   │                                                         │
 *   │  7. 返回结果                                            │
 *   │     {success, user_id, username, token}                  │
 *   └─────────────────────────────────────────────────────────┘
 */
std::string AuthServiceHandler::handleRegister(const std::string& username, 
                                               const std::string& password) {
    Json::Value response;
    response["type"] = "register";

    // Step 1: 验证输入
    if (username.length() < 3 || username.length() > 50) {
        response["success"] = false;
        response["message"] = "用户名长度必须在 3-50 个字符之间";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    if (password.length() < 6) {
        response["success"] = false;
        response["message"] = "密码长度至少 6 个字符";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // Step 2: 检查用户名是否存在
    auto guard = mysql_->getConn();
    std::string check_sql = "SELECT id FROM users WHERE username = '" + 
                           Utils::escape(username) + "'";
    
    if (!guard->query(check_sql)) {
        response["success"] = false;
        response["message"] = "数据库查询失败";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    if (guard->next()) {
        // 用户名已存在
        response["success"] = false;
        response["message"] = "用户名已被注册";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // Step 3: 密码哈希
    std::string password_hash = Utils::sha256(password);

    // Step 4: 插入数据库
    // 注意：使用 Prepared Statement 更安全，防止 SQL 注入
    // 这里简化了，实际上应该用参数化查询
    std::string insert_sql = "INSERT INTO users (username, password_hash) VALUES ('" +
                             Utils::escape(username) + "', '" + password_hash + "')";
    
    if (!guard->execute(insert_sql)) {
        response["success"] = false;
        response["message"] = "创建用户失败: " + guard->getError();
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // 获取自增 ID
    int user_id = guard->getInsertId();

    // Step 5: 生成 Token
    std::string token = Utils::generateToken(user_id, secret_);

    // Step 6: 存储会话到 Redis
    std::string session_key = "session:" + std::to_string(user_id);
    redis_->set(session_key, token, token_expire_seconds_);

    // Step 7: 返回成功结果
    response["success"] = true;
    response["message"] = "注册成功";
    response["data"]["user_id"] = user_id;
    response["data"]["username"] = username;
    response["data"]["token"] = token;

    LOG_INFO("用户注册成功: username=%s, user_id=%d", username.c_str(), user_id);
    return Json::writeString(Json::StreamWriterBuilder(), response);
}

// =====================================================
// 第三部分：用户登录
// =====================================================

/**
 * @brief 处理用户登录
 * 
 * 详细流程：
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  1. 验证输入                                            │
 *   │                                                         │
 *   │  2. 查询用户                                            │
 *   │     SELECT id, password_hash FROM users                 │
 *   │     WHERE username = ?                                  │
 *   │                                                         │
 *   │  3. 验证密码                                            │
 *   │     input_hash == stored_hash                           │
 *   │     - 对输入密码做同样哈希                               │
 *   │     - 和数据库存储的哈希比较                             │
 *   │                                                         │
 *   │  4. 生成新 Token                                        │
 *   │                                                         │
 *   │  5. 更新 Redis 会话                                     │
 *   │                                                         │
 *   │  6. 返回结果                                            │
 *   └─────────────────────────────────────────────────────────┘
 */
std::string AuthServiceHandler::handleLogin(const std::string& username, 
                                            const std::string& password) {
    Json::Value response;
    response["type"] = "login";

    // Step 1: 验证输入
    if (username.empty() || password.empty()) {
        response["success"] = false;
        response["message"] = "用户名和密码不能为空";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // Step 2: 查询用户
    auto guard = mysql_->getConn();
    std::string query_sql = "SELECT id, password_hash FROM users WHERE username = '" +
                           Utils::escape(username) + "'";
    
    if (!guard->query(query_sql)) {
        response["success"] = false;
        response["message"] = "数据库查询失败";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    if (!guard->next()) {
        // 用户不存在
        response["success"] = false;
        response["message"] = "用户名或密码错误";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    auto row = guard->nextRow();
    int user_id = std::stoi(row["id"]);
    std::string stored_hash = row["password_hash"];

    // Step 3: 验证密码
    std::string input_hash = Utils::sha256(password);
    if (input_hash != stored_hash) {
        // 密码错误（不告诉用户是用户名错还是密码错，安全考虑）
        response["success"] = false;
        response["message"] = "用户名或密码错误";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // Step 4: 生成新 Token
    std::string token = Utils::generateToken(user_id, secret_);

    // Step 5: 存储会话
    std::string session_key = "session:" + std::to_string(user_id);
    redis_->set(session_key, token, token_expire_seconds_);

    // Step 6: 返回成功结果
    response["success"] = true;
    response["message"] = "登录成功";
    response["data"]["user_id"] = user_id;
    response["data"]["username"] = username;
    response["data"]["token"] = token;

    LOG_INFO("用户登录成功: username=%s, user_id=%d", username.c_str(), user_id);
    return Json::writeString(Json::StreamWriterBuilder(), response);
}

// =====================================================
// 第四部分：验证 Token
// =====================================================

/**
 * @brief 验证 Token
 * 
 * 其他服务调用此接口验证用户身份
 * 
 * 流程：
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  1. 解析 Token                                          │
 *   │     Utils::verifyToken(token, secret)                   │
 *   │     - 验证签名                                          │
 *   │     - 检查过期时间                                       │
 *   │                                                         │
 *   │  2. 检查 Redis 会话                                    │
 *   │     GET session:{user_id}                               │
 *   │     - Token 是否和存储的一致                             │
 *   │     - 会话是否已过期                                    │
 *   │                                                         │
 *   │  3. 返回结果                                            │
 *   │     {success, user_id}                                  │
 *   └─────────────────────────────────────────────────────────┘
 */
std::string AuthServiceHandler::handleVerifyToken(const std::string& token) {
    Json::Value response;
    response["type"] = "verify_token";

    // Step 1: 解析和验证 Token
    int user_id = Utils::verifyToken(token, secret_);
    if (user_id < 0) {
        response["success"] = false;
        response["message"] = "无效的 Token";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // Step 2: 检查 Redis 会话
    std::string session_key = "session:" + std::to_string(user_id);
    std::string stored_token = redis_->get(session_key);
    
    if (stored_token.empty() || stored_token != token) {
        // Token 不存在或不匹配（可能已过期/被顶替）
        response["success"] = false;
        response["message"] = "会话已过期，请重新登录";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // Step 3: 返回成功结果
    response["success"] = true;
    response["data"]["user_id"] = user_id;

    LOG_DEBUG("Token 验证成功: user_id=%d", user_id);
    return Json::writeString(Json::StreamWriterBuilder(), response);
}

// =====================================================
// 第五部分：获取用户信息
// =====================================================

/**
 * @brief 获取用户信息
 * 
 * 使用缓存优先策略：
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  1. 查 Redis 缓存                                      │
 *   │     GET user:info:{user_id}                           │
 *   │                                                         │
 *   │  2. 命中缓存？                                         │
 *   │     - 是: 直接返回                                     │
 *   │     - 否: 查 MySQL                                    │
 *   │                                                         │
 *   │  3. 存入缓存                                          │
 *   │     SET user:info:{user_id} json_data                 │
 *   │     EXPIRE 3600                                       │
 *   │                                                         │
 *   │  4. 返回结果                                            │
 *   └─────────────────────────────────────────────────────────┘
 */
std::string AuthServiceHandler::handleGetUserInfo(int user_id) {
    Json::Value response;
    response["type"] = "get_user_info";

    // 获取用户信息（带缓存）
    auto user_info = getUserInfo(user_id);
    
    if (user_info.empty()) {
        response["success"] = false;
        response["message"] = "用户不存在";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // 返回结果
    response["success"] = true;
    response["data"]["user_id"] = user_id;
    response["data"]["username"] = user_info["username"];
    
    // 如果有创建时间也返回
    if (user_info.count("created_at")) {
        response["data"]["created_at"] = user_info["created_at"];
    }

    return Json::writeString(Json::StreamWriterBuilder(), response);
}

/**
 * @brief 获取用户信息（带缓存）
 */
std::map<std::string, std::string> AuthServiceHandler::getUserInfo(int user_id) {
    std::map<std::string, std::string> user_info;

    // Step 1: 查 Redis 缓存
    std::string cache_key = "user:info:" + std::to_string(user_id);
    std::string cached = redis_->get(cache_key);
    
    if (!cached.empty()) {
        // 命中缓存！直接解析返回
        Json::Value json;
        Json::Reader reader;
        if (reader.parse(cached, json)) {
            for (auto it = json.begin(); it != json.end(); ++it) {
                user_info[it.key().asString()] = it->asString();
            }
            return user_info;
        }
    }

    // Step 2: 查 MySQL
    auto guard = mysql_->getConn();
    std::string query_sql = "SELECT id, username, created_at FROM users WHERE id = " + 
                           std::to_string(user_id);
    
    if (!guard->query(query_sql) || !guard->next()) {
        return user_info;  // 空 map，表示用户不存在
    }

    auto row = guard->nextRow();
    user_info["id"] = row["id"];
    user_info["username"] = row["username"];
    if (row.count("created_at")) {
        user_info["created_at"] = row["created_at"];
    }

    // Step 3: 存入缓存
    Json::Value json_data;
    for (const auto& kv : user_info) {
        json_data[kv.first] = kv.second;
    }
    cacheUserInfo(user_id, user_info);

    return user_info;
}

/**
 * @brief 缓存用户信息
 */
void AuthServiceHandler::cacheUserInfo(int user_id, 
                                       const std::map<std::string, std::string>& user) {
    std::string cache_key = "user:info:" + std::to_string(user_id);
    
    // 转换为 JSON 字符串
    Json::Value json_data;
    for (const auto& kv : user) {
        json_data[kv.first] = kv.second;
    }
    
    std::string json_str = Json::writeString(Json::StreamWriterBuilder(), json_data);
    
    // 存入 Redis，设置过期时间
    redis_->set(cache_key, json_str, user_cache_ttl_);
}

// =====================================================
// 第六部分：刷新 Token
// =====================================================

/**
 * @brief 刷新 Token
 * 
 * 在 Token 过期前刷新，可以延长会话时间
 */
std::string AuthServiceHandler::handleRefreshToken(const std::string& old_token) {
    Json::Value response;
    response["type"] = "refresh_token";

    // Step 1: 验证旧 Token
    int user_id = Utils::verifyToken(old_token, secret_);
    if (user_id < 0) {
        response["success"] = false;
        response["message"] = "无效的 Token";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // Step 2: 检查旧 Token 是否有效
    std::string session_key = "session:" + std::to_string(user_id);
    std::string stored_token = redis_->get(session_key);
    
    if (stored_token != old_token) {
        response["success"] = false;
        response["message"] = "Token 不匹配";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // Step 3: 生成新 Token
    std::string new_token = Utils::generateToken(user_id, secret_);

    // Step 4: 更新 Redis 会话
    redis_->set(session_key, new_token, token_expire_seconds_);

    // Step 5: 返回新 Token
    response["success"] = true;
    response["message"] = "Token 已刷新";
    response["data"]["token"] = new_token;

    LOG_INFO("Token 刷新成功: user_id=%d", user_id);
    return Json::writeString(Json::StreamWriterBuilder(), response);
}

// =====================================================
// 使用示例（注释）
// =====================================================
/*
// 示例 1：用户注册
void example1(AuthServiceHandler& handler) {
    // 注册新用户
    std::string result = handler.handleRegister("alice", "password123");
    
    // 解析结果
    Json::Value json;
    Json::Reader().parse(result, json);
    
    if (json["success"].asBool()) {
        int user_id = json["data"]["user_id"].asInt();
        std::string token = json["data"]["token"].asString();
        std::cout << "注册成功！用户ID: " << user_id << std::endl;
    }
}

// 示例 2：用户登录
void example2(AuthServiceHandler& handler) {
    std::string result = handler.handleLogin("alice", "password123");
    
    Json::Value json;
    Json::Reader().parse(result, json);
    
    if (json["success"].asBool()) {
        std::cout << "登录成功！" << std::endl;
        std::cout << "Token: " << json["data"]["token"].asString() << std::endl;
    }
}

// 示例 3：验证 Token
void example3(AuthServiceHandler& handler) {
    std::string token = "eyJhbGciOi...";  // 从某处获取的 Token
    
    std::string result = handler.handleVerifyToken(token);
    
    Json::Value json;
    Json::Reader().parse(result, json);
    
    if (json["success"].asBool()) {
        int user_id = json["data"]["user_id"].asInt();
        std::cout << "Token 有效，用户ID: " << user_id << std::endl;
    } else {
        std::cout << "Token 无效: " << json["message"].asString() << std::endl;
    }
}

// 示例 4：获取用户信息（带缓存）
void example4(AuthServiceHandler& handler) {
    // 第一次查询：查 MySQL + 存入缓存
    std::string result1 = handler.handleGetUserInfo(123);
    
    // 第二次查询：命中缓存
    std::string result2 = handler.handleGetUserInfo(123);
    // 这次会从 Redis 缓存读取，速度更快
}
*/
