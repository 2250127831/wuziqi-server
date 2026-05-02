/**
 * @file redis_pool.h
 * @brief Redis 连接池封装
 * 
 * =====================================================
 * 什么是 Redis？
 * =====================================================
 * 
 * Redis 是一个「内存数据库」：
 * - 数据存在内存里，读写速度极快
 * - 支持 String、Hash、List、Set 等数据结构
 * - 常用于：缓存、Session 存储、实时排行榜、消息队列
 * 
 * =====================================================
 * 什么是 Redis 连接池？
 * =====================================================
 * 
 * 和 MySQL 连接池一样的思想：
 * - 预先创建一组 Redis 连接
 * - 复用连接，避免每次操作都创建/销毁
 * - 减少 TCP 连接的开销
 * 
 * Redis 连接池 vs MySQL 连接池：
 * - MySQL: 处理结构化数据（表、行、列）
 * - Redis: 处理键值对（key → value）和各种数据结构
 */

#ifndef REDIS_POOL_H
#define REDIS_POOL_H

#include <string>      // std::string
#include <vector>      // std::vector
#include <memory>      // std::unique_ptr
#include <functional>  // std::function
#include <mutex>       // std::mutex
#include <queue>       // std::queue（FIFO 队列）

// =====================================================
// 第一部分：配置结构体
// =====================================================
/**
 * @struct redis_pool_config
 * @brief Redis 连接池的配置参数
 * 
 * Redis 配置和 MySQL 有些不同：
 * - Redis 不需要用户名（默认不需要认证）
 * - 有密码的话需要 AUTH
 * - 可以选择 DB 编号（类似 MySQL 的 database）
 */
struct redis_pool_config {
    std::string host = "127.0.0.1";      // Redis 服务器地址
    int port = 6379;                      // Redis 端口（默认 6379）
    std::string password;                 // 密码（可选，没有则为空）
    int db = 0;                           // 数据库编号（0-15，默认 0）
    int pool_size = 16;                   // 连接池大小
    int timeout_ms = 3000;               // 超时时间（毫秒）
};

// =====================================================
// 第二部分：前向声明
// =====================================================
class RedisConn;

// =====================================================
// 第三部分：RedisPool 连接池类
// =====================================================
/**
 * @class RedisPool
 * @brief Redis 连接池管理器
 * 
 * 架构图：
 * 
 *   ┌─────────────────────────────────────────────┐
 *   │              RedisPool                       │
 *   ├─────────────────────────────────────────────┤
 *   │                                             │
 *   │  ┌─ 可用队列 available_ (queue) ─┐         │
 *   │  │  front → [conn] → [conn] → back│        │
 *   │  │           pop()                 │        │
 *   │  └─────────────────────────────────┘        │
 *   │                                             │
 *   │  ┌─ 全部连接 all_conns_ ─┐                 │
 *   │  │  [conn][conn][conn]...│                 │
 *   │  └────────────────────────┘                 │
 *   │                                             │
 *   │  ┌─ Guard (RAII) ───────┐                   │
 *   │  │  自动归还连接        │                   │
 *   │  └─────────────────────┘                   │
 *   └─────────────────────────────────────────────┘
 * 
 * 与 MySQL 连接池的区别：
 * - 存储可用连接：MySQL 用 vector，Redis 用 queue
 * - 取出方式：MySQL 用 pop_back()（LIFO），Redis 用 pop()（FIFO）
 */
class RedisPool {
public:
    /**
     * @brief 构造函数
     */
    explicit RedisPool(const redis_pool_config& config);
    
    /**
     * @brief 析构函数 - 关闭所有连接
     */
    ~RedisPool();

    // =====================================================
    // 初始化
    // =====================================================
    /**
     * @brief 初始化连接池
     * @return true 成功，false 失败
     */
    bool init();

    // =====================================================
    // Guard 类 - RAII 自动归还
    // =====================================================
    /**
     * @class Guard
     * @brief RAII 资源管理类（和 MySQL 连接池一样）
     * 
     * 原理：
     * - 构造函数获取连接
     * - 析构函数自动归还连接
     */
    class Guard {
    public:
        Guard(RedisPool* pool, RedisConn* conn) 
            : pool_(pool), conn_(conn) {}

        /**
         * @brief 析构函数 - 自动归还连接！
         */
        ~Guard() { 
            if (pool_ && conn_) pool_->returnConn(conn_); 
        }

        RedisConn* get() const { return conn_; }
        RedisConn* operator->() const { return conn_; }

    private:
        RedisPool* pool_;
        RedisConn* conn_;
    };

    /**
     * @brief 获取一个连接
     * @return Guard 对象
     */
    Guard getConn();

    // =====================================================
    // String 操作
    // =====================================================
    /**
     * @brief 获取值
     * @param key 键
     * @return 值（如果不存在返回空字符串）
     * 
     * Redis 命令：GET key
     * 示例：
     *   redis->get("username")  // 返回 "alice"
     */
    std::string get(const std::string& key);
    
    /**
     * @brief 设置值
     * @param key 键
     * @param value 值
     * @param ttl_seconds 过期时间（秒，0 表示永不过期）
     * @return true 成功
     * 
     * Redis 命令：SET key value [EX seconds]
     * 示例：
     *   redis->set("username", "alice");           // 永不过期
     *   redis->set("token", "abc123", 3600);       // 1小时后过期
     */
    bool set(const std::string& key, const std::string& value, int ttl_seconds = 0);
    
    /**
     * @brief 删除键
     * @param key 键
     * @return true 成功
     * 
     * Redis 命令：DEL key
     */
    bool del(const std::string& key);
    
    /**
     * @brief 检查键是否存在
     * @param key 键
     * @return true 存在
     * 
     * Redis 命令：EXISTS key
     */
    bool exists(const std::string& key);
    
    /**
     * @brief 递增
     * @param key 键（值必须是数字）
     * @return 递增后的值
     * 
     * Redis 命令：INCR key
     * 示例：
     *   redis->incr("count");  // count 从 0 变成 1
     */
    long long incr(const std::string& key);
    
    /**
     * @brief 递减
     * @param key 键
     * @return 递减后的值
     * 
     * Redis 命令：DECR key
     */
    long long decr(const std::string& key);

    // =====================================================
    // Hash 操作
    // =====================================================
    /**
     * @brief 设置 Hash 字段
     * @param key Hash 的键
     * @param field 字段名
     * @param value 字段值
     * @return true 成功
     * 
     * Redis 命令：HSET key field value
     * 示例：
     *   redis->hset("user:1", "name", "alice");
     *   redis->hset("user:1", "age", "20");
     */
    bool hset(const std::string& key, const std::string& field, const std::string& value);
    
    /**
     * @brief 获取 Hash 字段
     * @param key Hash 的键
     * @param field 字段名
     * @return 字段值
     * 
     * Redis 命令：HGET key field
     */
    std::string hget(const std::string& key, const std::string& field);
    
    /**
     * @brief 批量设置 Hash 字段
     * @param key Hash 的键
     * @param fields 字段列表 {field1, value1, field2, value2, ...}
     * @return true 成功
     * 
     * Redis 命令：HMSET key field1 value1 field2 value2 ...
     */
    bool hmset(const std::string& key, 
               const std::vector<std::pair<std::string, std::string>>& fields);
    
    /**
     * @brief 获取所有 Hash 字段
     * @param key Hash 的键
     * @return 字段值列表 [field1, value1, field2, value2, ...]
     * 
     * Redis 命令：HGETALL key
     * 注意：返回的是 [field1, value1, field2, value2, ...]
     *       不是 map，需要自己处理
     */
    std::vector<std::string> hgetall(const std::string& key);
    
    /**
     * @brief 删除 Hash 字段
     * @param key Hash 的键
     * @param field 字段名
     * @return true 成功
     * 
     * Redis 命令：HDEL key field
     */
    bool hdel(const std::string& key, const std::string& field);
    
    /**
     * @brief 检查 Hash 字段是否存在
     * @param key Hash 的键
     * @param field 字段名
     * @return true 存在
     * 
     * Redis 命令：HEXISTS key field
     */
    bool hexists(const std::string& key, const std::string& field);

    // =====================================================
    // List 操作
    // =====================================================
    /**
     * @brief 左插入（头部）
     * @param key List 的键
     * @param value 要插入的值
     * @return true 成功
     * 
     * Redis 命令：LPUSH key value
     * 示例：
     *   redis->lpush("tasks", "task1");  // List: [task1]
     *   redis->lpush("tasks", "task2");  // List: [task2, task1]
     */
    bool lpush(const std::string& key, const std::string& value);
    
    /**
     * @brief 左弹出（头部）
     * @param key List 的键
     * @return 弹出的值
     * 
     * Redis 命令：LPOP key
     */
    std::string lpop(const std::string& key);
    
    /**
     * @brief 右插入（尾部）
     * @param key List 的键
     * @param value 要插入的值
     * @return true 成功
     * 
     * Redis 命令：RPUSH key value
     */
    bool rpush(const std::string& key, const std::string& value);
    
    /**
     * @brief 右弹出（尾部）
     * @param key List 的键
     * @return 弹出的值
     * 
     * Redis 命令：RPOP key
     */
    std::string rpop(const std::string& key);
    
    /**
     * @brief 获取范围元素
     * @param key List 的键
     * @param start 起始索引
     * @param stop 结束索引（-1 表示到末尾）
     * @return 元素列表
     * 
     * Redis 命令：LRANGE key start stop
     * 示例：
     *   redis->lrange("tasks", 0, -1);  // 获取所有
     */
    std::vector<std::string> lrange(const std::string& key, int start, int stop);
    
    /**
     * @brief 获取 List 长度
     * @param key List 的键
     * @return 长度
     * 
     * Redis 命令：LLEN key
     */
    long long llen(const std::string& key);
    
    /**
     * @brief 删除元素
     * @param key List 的键
     * @param count 删除数量
     * @param value 要删除的值
     * @return 删除的数量
     * 
     * Redis 命令：LREM key count value
     */
    long long lrem(const std::string& key, int count, const std::string& value);

    // =====================================================
    // Set 操作
    // =====================================================
    /**
     * @brief 添加到 Set
     * @param key Set 的键
     * @param member 成员
     * @return true 成功
     * 
     * Redis 命令：SADD key member
     */
    bool sadd(const std::string& key, const std::string& member);
    
    /**
     * @brief 从 Set 删除
     * @param key Set 的键
     * @param member 成员
     * @return true 成功
     * 
     * Redis 命令：SREM key member
     */
    bool srem(const std::string& key, const std::string& member);
    
    /**
     * @brief 检查是否在 Set 中
     * @param key Set 的键
     * @param member 成员
     * @return true 在集合中
     * 
     * Redis 命令：SISMEMBER key member
     */
    bool sismember(const std::string& key, const std::string& member);
    
    /**
     * @brief 获取所有成员
     * @param key Set 的键
     * @return 成员列表
     * 
     * Redis 命令：SMEMBERS key
     */
    std::vector<std::string> smembers(const std::string& key);

    // =====================================================
    // 通用执行
    // =====================================================
    /**
     * @brief 执行任意 Redis 命令
     * @param args 命令参数列表 [cmd, arg1, arg2, ...]
     * @return 结果列表
     * 
     * 示例：
     *   redis->exec({"LPUSH", "queue", "msg1"});
     *   redis->exec({"HGETALL", "user:1"});
     */
    std::vector<std::string> exec(const std::vector<std::string>& args);

    // =====================================================
    // 状态查询
    // =====================================================
    size_t getPoolSize() const { return pool_size_; }
    size_t getActiveCount() const;

// =====================================================
// 私有成员
// =====================================================
private:
    RedisConn* allocate();
    void returnConn(RedisConn* conn);

    redis_pool_config config_;           // 配置
    size_t pool_size_;                  // 池大小
    std::queue<RedisConn*> available_;  // 可用连接队列（注意：用 queue 而不是 vector）
    std::vector<RedisConn*> all_conns_; // 全部连接
    mutable std::mutex mutex_;           // 互斥锁
};

// =====================================================
// 第四部分：RedisConn 单个连接类
// =====================================================
/**
 * @class RedisConn
 * @brief 对原生 Redis 连接的封装
 * 
 * 使用 hiredis 库（C 语言实现）
 * - redisContext: 连接上下文
 * - redisCommand: 执行命令
 * - redisReply: 命令响应
 */
class RedisConn {
public:
    /**
     * @brief 友元声明
     * 
     * 让 RedisPool 可以访问私有成员
     */
    friend class RedisPool;
    
    ~RedisConn();

    /**
     * @brief 建立连接
     */
    bool connect(const std::string& host, int port, 
                 const std::string& password, int db, int timeout_ms);
    
    /**
     * @brief 断开连接
     */
    void disconnect();

    /**
     * @brief 执行单条命令
     * @param cmd 命令字符串（如 "GET key"）
     * @return 结果字符串
     * 
     * 注意：这个方法不方便传参，一般用 exec()
     */
    std::string command(const std::string& cmd);
    
    /**
     * @brief 执行命令（推荐方式）
     * @param args 参数列表
     * @return 结果列表
     * 
     * 更安全，不会被注入
     */
    std::vector<std::string> exec(const std::vector<std::string>& args);

    bool isConnected() const { return connected_; }

private:
    struct redisContext* ctx_ = nullptr;  // Redis 连接上下文
    bool connected_ = false;              // 连接状态
    std::string error_;                   // 错误信息
};

#endif // REDIS_POOL_H
