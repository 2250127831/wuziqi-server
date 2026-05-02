/**
 * @file redis_pool.cpp
 * @brief Redis 连接池实现
 * 
 * 基于 hiredis 库（Redis 官方 C 客户端）
 */

#include "redis_pool.h"
#include "logger.h"
#include <hiredis/hiredis.h>   // hiredis 头文件
#include <cstring>              // strlen

// =====================================================
// 第一部分：RedisConn 实现
// =====================================================

/**
 * @brief 析构函数
 */
RedisConn::~RedisConn() {
    disconnect();
}

/**
 * @brief 建立 Redis 连接
 * @param host 服务器地址
 * @param port 端口
 * @param password 密码（可选）
 * @param db 数据库编号
 * @param timeout_ms 超时（毫秒）
 * @return true 成功
 * 
 * 连接流程：
 * 
 *   ┌─────────────────────────────────────────────┐
 *   │  1. 设置超时参数                            │
 *   │  2. redisConnectWithTimeout() 建立连接      │
 *   │  3. 如果需要密码，执行 AUTH 命令            │
 *   │  4. 如果 db != 0，执行 SELECT 命令          │
 *   └─────────────────────────────────────────────┘
 */
bool RedisConn::connect(const std::string& host, int port, 
                       const std::string& password, int db, int timeout_ms) {
    // 先断开已有连接
    disconnect();

    // Step 1: 设置超时参数
    // timeval 结构：tv_sec = 秒, tv_usec = 微秒
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;           // 3000ms → 3秒
    tv.tv_usec = (timeout_ms % 1000) * 1000; // 毫秒 → 微秒

    // Step 2: 建立连接
    // redisConnectWithTimeout() 是 redisConnect() 的超时版本
    ctx_ = redisConnectWithTimeout(host.c_str(), port, tv);
    
    // 检查连接是否成功
    if (!ctx_ || ctx_->err) {
        // 连接失败！
        if (ctx_) {
            error_ = ctx_->errstr;  // 获取错误信息
            redisFree(ctx_);       // 清理资源
            ctx_ = nullptr;
        } else {
            error_ = "redisConnect failed";
        }
        return false;
    }

    // Step 3: 密码认证（如果有密码）
    if (!password.empty()) {
        // redisCommand() 执行 Redis 命令
        // 相当于在 redis-cli 中执行：AUTH password
        redisReply* reply = (redisReply*)redisCommand(ctx_, "AUTH %s", password.c_str());
        
        // 检查认证结果
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            LOG_ERROR("Redis 认证失败: %s", reply ? reply->str : "unknown");
            if (reply) freeReplyObject(reply);
            disconnect();
            return false;
        }
        freeReplyObject(reply);  // 释放响应内存
    }

    // Step 4: 选择数据库（如果不用默认 DB 0）
    if (db != 0) {
        // 相当于：SELECT 0（Redis 默认是 DB 0）
        redisReply* reply = (redisReply*)redisCommand(ctx_, "SELECT %d", db);
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            LOG_ERROR("Redis 选择数据库失败: %s", reply ? reply->str : "unknown");
            if (reply) freeReplyObject(reply);
            disconnect();
            return false;
        }
        freeReplyObject(reply);
    }

    connected_ = true;
    return true;
}

/**
 * @brief 断开连接
 */
void RedisConn::disconnect() {
    if (ctx_) {
        redisFree(ctx_);  // 释放连接
        ctx_ = nullptr;
    }
    connected_ = false;
}

/**
 * @brief 执行单条命令（简单但不安全）
 * @param cmd 命令字符串
 * @return 结果字符串
 * 
 * 为什么不安全？
 * 命令和参数混在一起，无法防止注入
 * 
 * 示例：
 *   command("GET username");        // ✅ 安全
 *   command("GET " + username);     // ❌ 危险！
 *   command("SET key " + value);    // ❌ value 里有空格就麻烦了
 */
std::string RedisConn::command(const std::string& cmd) {
    if (!connected_) return "";

    // 执行命令
    redisReply* reply = (redisReply*)redisCommand(ctx_, cmd.c_str());
    if (!reply) return "";

    std::string result;
    
    // 解析响应
    if (reply->type == REDIS_REPLY_STRING) {
        // 字符串类型响应
        result = std::string(reply->str, reply->len);
    } else if (reply->type == REDIS_REPLY_INTEGER) {
        // 整数类型响应
        result = std::to_string(reply->integer);
    }
    // 其他类型（数组、错误等）返回空字符串

    freeReplyObject(reply);
    return result;
}

/**
 * @brief 执行命令（推荐方式）
 * @param args 参数列表
 * @return 结果列表
 * 
 * 使用 redisCommandArgv()，参数分开传递，更安全
 * 
 * 示例：
 *   exec({"GET", "username"});                    // 获取值
 *   exec({"SET", "name", "alice", "EX", "3600"}); // 设置值，1小时过期
 *   exec({"HMSET", "user:1", "name", "alice", "age", "20"});
 */
std::vector<std::string> RedisConn::exec(const std::vector<std::string>& args) {
    std::vector<std::string> result;

    if (!connected_ || args.empty()) return result;

    // 准备参数数组
    // redisCommandArgv() 需要两个数组：
    // - argv: 参数指针数组
    // - argvlen: 每个参数的长度
    std::vector<const char*> argv;
    std::vector<size_t> argvlen;
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
        argvlen.push_back(arg.size());
    }

    // 执行命令
    redisReply* reply = (redisReply*)redisCommandArgv(
        ctx_, 
        argv.size(),    // 参数个数
        argv.data(),    // 参数指针数组
        argvlen.data()  // 参数长度数组
    );
    if (!reply) return result;

    // 解析数组响应
    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            redisReply* elem = reply->element[i];
            
            if (elem->type == REDIS_REPLY_STRING) {
                // 字符串类型
                result.emplace_back(elem->str, elem->len);
            } else if (elem->type == REDIS_REPLY_INTEGER) {
                // 整数类型
                result.push_back(std::to_string(elem->integer));
            } else if (elem->type == REDIS_REPLY_NIL) {
                // 空值（nil）
                result.emplace_back();  // 添加空字符串
            }
        }
    }

    freeReplyObject(reply);
    return result;
}

// =====================================================
// 第二部分：RedisPool 实现
// =====================================================

/**
 * @brief 构造函数
 */
RedisPool::RedisPool(const redis_pool_config& config)
    : config_(config), 
      pool_size_(config.pool_size) {
}

/**
 * @brief 析构函数
 */
RedisPool::~RedisPool() {
    for (auto conn : all_conns_) {
        delete conn;
    }
}

/**
 * @brief 初始化连接池
 * @return true 成功
 * 
 * 和 MySQL 连接池一样：
 * 1. 创建 pool_size 个连接
 * 2. 每个都调用 connect()
 * 3. 放入 available_ 队列
 */
bool RedisPool::init() {
    LOG_INFO("初始化 Redis 连接池: %s:%d, 大小: %d",
             config_.host.c_str(), 
             config_.port, 
             pool_size_);

    for (size_t i = 0; i < pool_size_; ++i) {
        auto conn = new RedisConn();
        
        if (!conn->connect(config_.host, config_.port, config_.password,
                           config_.db, config_.timeout_ms)) {
            LOG_ERROR("Redis 连接失败: %s", config_.host.c_str());
            delete conn;
            return false;
        }
        
        // 注意：这里用的是 queue，所以用 push()
        // MySQL 用的是 vector，所以用 push_back()
        available_.push(conn);      // 入队
        all_conns_.push_back(conn); // 追踪
    }

    LOG_INFO("Redis 连接池初始化成功");
    return true;
}

/**
 * @brief 获取连接
 * @return Guard 对象
 * 
 * 流程：
 * 1. 从 available_ 队列取出一个（front + pop）
 * 2. 没有则创建新的
 * 3. 返回 Guard
 * 
 * 注意：MySQL 用的是 vector + back() + pop_back()
 *       Redis 用的是 queue + front() + pop()
 * 
 * 为什么用不同的数据结构？
 * - vector + LIFO：刚还回来的连接缓存更热，可能马上又要用
 * - queue + FIFO：先借的先还，公平调度
 */
RedisPool::Guard RedisPool::getConn() {
    RedisConn* conn = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!available_.empty()) {
            // 从队列头部取出
            conn = available_.front();
            available_.pop();
        }
    }

    // 没有可用连接则扩容
    if (!conn) {
        conn = allocate();
    }

    return Guard(this, conn);
}

/**
 * @brief 分配新连接（扩容）
 */
RedisConn* RedisPool::allocate() {
    auto conn = new RedisConn();
    if (!conn->connect(config_.host, config_.port, config_.password,
                       config_.db, config_.timeout_ms)) {
        LOG_ERROR("Redis 扩容连接失败");
        delete conn;
        return nullptr;
    }
    return conn;
}

/**
 * @brief 归还连接
 */
void RedisPool::returnConn(RedisConn* conn) {
    if (!conn) return;

    std::lock_guard<std::mutex> lock(mutex_);
    
    // 如果池未满，归还到队列
    if (available_.size() < pool_size_) {
        available_.push(conn);
    } else {
        // 超过池大小，销毁
        delete conn;
    }
}

/**
 * @brief 获取活跃连接数
 */
size_t RedisPool::getActiveCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_size_ - available_.size();
}

// =====================================================
// 第三部分：String 操作实现
// =====================================================

/**
 * @brief GET key
 */
std::string RedisPool::get(const std::string& key) {
    auto guard = getConn();
    
    // 使用 hiredis 的便捷方式
    auto reply = (redisReply*)redisCommand(guard->ctx_, "GET %s", key.c_str());
    if (!reply) return "";
    
    // 提取字符串结果
    std::string result = (reply->type == REDIS_REPLY_STRING) 
        ? std::string(reply->str, reply->len) 
        : "";
    
    freeReplyObject(reply);
    return result;
}

/**
 * @brief SET key value [EX seconds]
 */
bool RedisPool::set(const std::string& key, const std::string& value, int ttl_seconds) {
    auto guard = getConn();
    redisReply* reply;
    
    if (ttl_seconds > 0) {
        // SET key value EX seconds
        reply = (redisReply*)redisCommand(guard->ctx_, 
            "SETEX %s %d %s", key.c_str(), ttl_seconds, value.c_str());
    } else {
        // SET key value
        reply = (redisReply*)redisCommand(guard->ctx_, 
            "SET %s %s", key.c_str(), value.c_str());
    }
    
    if (!reply) return false;
    
    // REDIS_REPLY_ERROR 表示失败
    bool success = reply->type != REDIS_REPLY_ERROR;
    
    freeReplyObject(reply);
    return success;
}

/**
 * @brief DEL key
 */
bool RedisPool::del(const std::string& key) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, "DEL %s", key.c_str());
    if (!reply) return false;
    freeReplyObject(reply);
    return true;
}

/**
 * @brief EXISTS key
 */
bool RedisPool::exists(const std::string& key) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, "EXISTS %s", key.c_str());
    if (!reply) return false;
    
    // EXISTS 返回整数：1 存在，0 不存在
    bool exists = reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    
    freeReplyObject(reply);
    return exists;
}

/**
 * @brief INCR key
 */
long long RedisPool::incr(const std::string& key) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, "INCR %s", key.c_str());
    if (!reply) return 0;
    
    long long result = (reply->type == REDIS_REPLY_INTEGER) ? reply->integer : 0;
    
    freeReplyObject(reply);
    return result;
}

/**
 * @brief DECR key
 */
long long RedisPool::decr(const std::string& key) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, "DECR %s", key.c_str());
    if (!reply) return 0;
    
    long long result = (reply->type == REDIS_REPLY_INTEGER) ? reply->integer : 0;
    
    freeReplyObject(reply);
    return result;
}

// =====================================================
// 第四部分：Hash 操作实现
// =====================================================

/**
 * @brief HSET key field value
 */
bool RedisPool::hset(const std::string& key, const std::string& field, 
                     const std::string& value) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, 
        "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());
    if (!reply) return false;
    freeReplyObject(reply);
    return true;
}

/**
 * @brief HGET key field
 */
std::string RedisPool::hget(const std::string& key, const std::string& field) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, 
        "HGET %s %s", key.c_str(), field.c_str());
    if (!reply) return "";
    
    std::string result = (reply->type == REDIS_REPLY_STRING) 
        ? std::string(reply->str, reply->len) 
        : "";
    
    freeReplyObject(reply);
    return result;
}

/**
 * @brief HMSET key field1 value1 field2 value2 ...
 */
bool RedisPool::hmset(const std::string& key, 
                      const std::vector<std::pair<std::string, std::string>>& fields) {
    auto guard = getConn();
    
    // 使用 exec() 方法更清晰
    std::vector<const char*> argv;
    std::vector<size_t> argvlen;
    
    argv.push_back("HMSET");
    argvlen.push_back(5);
    argv.push_back(key.c_str());
    argvlen.push_back(key.size());
    
    for (const auto& f : fields) {
        argv.push_back(f.first.c_str());
        argvlen.push_back(f.first.size());
        argv.push_back(f.second.c_str());
        argvlen.push_back(f.second.size());
    }
    
    auto reply = (redisReply*)redisCommandArgv(
        guard->ctx_, argv.size(), argv.data(), argvlen.data());
    
    if (!reply) return false;
    freeReplyObject(reply);
    return true;
}

/**
 * @brief HGETALL key
 */
std::vector<std::string> RedisPool::hgetall(const std::string& key) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, "HGETALL %s", key.c_str());
    
    std::vector<std::string> result;
    if (!reply) return result;
    
    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            auto elem = reply->element[i];
            if (elem->type == REDIS_REPLY_STRING) {
                result.emplace_back(elem->str, elem->len);
            }
        }
    }

    freeReplyObject(reply);
    return result;
}

/**
 * @brief HDEL key field
 */
bool RedisPool::hdel(const std::string& key, const std::string& field) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, 
        "HDEL %s %s", key.c_str(), field.c_str());
    if (!reply) return false;
    freeReplyObject(reply);
    return true;
}

/**
 * @brief HEXISTS key field
 */
bool RedisPool::hexists(const std::string& key, const std::string& field) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, 
        "HEXISTS %s %s", key.c_str(), field.c_str());
    if (!reply) return false;
    
    bool exists = reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    
    freeReplyObject(reply);
    return exists;
}

// =====================================================
// 第五部分：List 操作实现
// =====================================================

/**
 * @brief LPUSH key value
 */
bool RedisPool::lpush(const std::string& key, const std::string& value) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, 
        "LPUSH %s %s", key.c_str(), value.c_str());
    if (!reply) return false;
    freeReplyObject(reply);
    return true;
}

/**
 * @brief LPOP key
 */
std::string RedisPool::lpop(const std::string& key) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, "LPOP %s", key.c_str());
    if (!reply) return "";
    
    std::string result = (reply->type == REDIS_REPLY_STRING) 
        ? std::string(reply->str, reply->len) 
        : "";
    
    freeReplyObject(reply);
    return result;
}

/**
 * @brief RPUSH key value
 */
bool RedisPool::rpush(const std::string& key, const std::string& value) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, 
        "RPUSH %s %s", key.c_str(), value.c_str());
    if (!reply) return false;
    freeReplyObject(reply);
    return true;
}

/**
 * @brief RPOP key
 */
std::string RedisPool::rpop(const std::string& key) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, "RPOP %s", key.c_str());
    if (!reply) return "";
    
    std::string result = (reply->type == REDIS_REPLY_STRING) 
        ? std::string(reply->str, reply->len) 
        : "";
    
    freeReplyObject(reply);
    return result;
}

/**
 * @brief LRANGE key start stop
 */
std::vector<std::string> RedisPool::lrange(const std::string& key, int start, int stop) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, 
        "LRANGE %s %d %d", key.c_str(), start, stop);
    
    std::vector<std::string> result;
    if (!reply) return result;
    
    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            auto elem = reply->element[i];
            if (elem->type == REDIS_REPLY_STRING) {
                result.emplace_back(elem->str, elem->len);
            }
        }
    }

    freeReplyObject(reply);
    return result;
}

/**
 * @brief LLEN key
 */
long long RedisPool::llen(const std::string& key) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, "LLEN %s", key.c_str());
    if (!reply) return 0;
    
    long long result = (reply->type == REDIS_REPLY_INTEGER) ? reply->integer : 0;
    
    freeReplyObject(reply);
    return result;
}

/**
 * @brief LREM key count value
 */
long long RedisPool::lrem(const std::string& key, int count, const std::string& value) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, 
        "LREM %s %d %s", key.c_str(), count, value.c_str());
    if (!reply) return 0;
    
    long long result = (reply->type == REDIS_REPLY_INTEGER) ? reply->integer : 0;
    
    freeReplyObject(reply);
    return result;
}

// =====================================================
// 第六部分：Set 操作实现
// =====================================================

/**
 * @brief SADD key member
 */
bool RedisPool::sadd(const std::string& key, const std::string& member) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, 
        "SADD %s %s", key.c_str(), member.c_str());
    if (!reply) return false;
    freeReplyObject(reply);
    return true;
}

/**
 * @brief SREM key member
 */
bool RedisPool::srem(const std::string& key, const std::string& member) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, 
        "SREM %s %s", key.c_str(), member.c_str());
    if (!reply) return false;
    freeReplyObject(reply);
    return true;
}

/**
 * @brief SISMEMBER key member
 */
bool RedisPool::sismember(const std::string& key, const std::string& member) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, 
        "SISMEMBER %s %s", key.c_str(), member.c_str());
    if (!reply) return false;
    
    bool exists = reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    
    freeReplyObject(reply);
    return exists;
}

/**
 * @brief SMEMBERS key
 */
std::vector<std::string> RedisPool::smembers(const std::string& key) {
    auto guard = getConn();
    auto reply = (redisReply*)redisCommand(guard->ctx_, "SMEMBERS %s", key.c_str());
    
    std::vector<std::string> result;
    if (!reply) return result;
    
    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            auto elem = reply->element[i];
            if (elem->type == REDIS_REPLY_STRING) {
                result.emplace_back(elem->str, elem->len);
            }
        }
    }

    freeReplyObject(reply);
    return result;
}

// =====================================================
// 第七部分：通用执行
// =====================================================

/**
 * @brief 执行任意命令
 */
std::vector<std::string> RedisPool::exec(const std::vector<std::string>& args) {
    auto guard = getConn();
    
    std::vector<const char*> argv;
    std::vector<size_t> argvlen;
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
        argvlen.push_back(arg.size());
    }
    
    auto reply = (redisReply*)redisCommandArgv(
        guard->ctx_, argv.size(), argv.data(), argvlen.data());
    
    std::vector<std::string> result;
    if (!reply) return result;
    
    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            auto elem = reply->element[i];
            if (elem->type == REDIS_REPLY_STRING) {
                result.emplace_back(elem->str, elem->len);
            } else if (elem->type == REDIS_REPLY_INTEGER) {
                result.push_back(std::to_string(elem->integer));
            } else if (elem->type == REDIS_REPLY_NIL) {
                result.emplace_back();
            }
        }
    }

    freeReplyObject(reply);
    return result;
}

// =====================================================
// 使用示例（注释）
// =====================================================
/*
// 示例 1：简单的 String 操作
void example1(RedisPool& redis) {
    redis.set("name", "alice");           // SET name alice
    std::string name = redis.get("name"); // GET name
    redis.del("name");                    // DEL name
}

// 示例 2：带过期时间的缓存
void example2(RedisPool& redis) {
    // 登录 token，1小时后过期
    redis.set("token:user123", "abc123xyz", 3600);
    
    // 检查 token 是否存在
    if (redis.exists("token:user123")) {
        std::string token = redis.get("token:user123");
    }
}

// 示例 3：计数器
void example3(RedisPool& redis) {
    long long count = redis.incr("page_views");  // INCR page_views
    std::cout << "访问量: " << count << std::endl;
    
    count = redis.decr("page_views");  // DECR page_views
    std::cout << "访问量: " << count << std::endl;
}

// 示例 4：Hash 存储用户信息
void example4(RedisPool& redis) {
    // HMSET user:1 name alice age 20 city beijing
    redis.hmset("user:1", {
        {"name", "alice"},
        {"age", "20"},
        {"city", "beijing"}
    });
    
    // 获取单个字段
    std::string age = redis.hget("user:1", "age");  // "20"
    
    // 获取所有字段
    auto fields = redis.hgetall("user:1");
    // 返回: ["name", "alice", "age", "20", "city", "beijing"]
    // 需要两两一组解析
    for (size_t i = 0; i + 1 < fields.size(); i += 2) {
        std::cout << fields[i] << ": " << fields[i + 1] << std::endl;
    }
}

// 示例 5：消息队列
void example5(RedisPool& redis) {
    // 生产者：右插入
    redis.rpush("task_queue", "task1");
    redis.rpush("task_queue", "task2");
    
    // 消费者：左弹出
    while (true) {
        std::string task = redis.lpop("task_queue");
        if (task.empty()) break;  // 队列为空
        process(task);
    }
}

// 示例 6：Set 去重
void example6(RedisPool& redis) {
    // 添加投票记录
    redis.sadd("voted:2024", "user1");
    redis.sadd("voted:2024", "user2");
    redis.sadd("voted:2024", "user1");  // 重复，不会加入
    
    // 检查是否已投票
    if (redis.sismember("voted:2024", "user1")) {
        std::cout << "user1 已经投过票了" << std::endl;
    }
    
    // 获取所有投票者
    auto voters = redis.smembers("voted:2024");
}
*/
