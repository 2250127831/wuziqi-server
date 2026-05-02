/**
 * @file mysql_pool.h
 * @brief MySQL 连接池封装
 * 
 * =====================================================
 * 什么是连接池？
 * =====================================================
 * 
 * 没有连接池时：
 *   请求1 → 创建连接(50ms) → 查询(10ms) → 销毁连接(20ms) = 80ms
 *   请求2 → 创建连接(50ms) → 查询(10ms) → 销毁连接(20ms) = 80ms
 * 
 * 有连接池时：
 *   请求1 → 获取连接(1ms) → 查询(10ms) → 归还连接(1ms) = 12ms
 *   请求2 → 获取连接(1ms) → 查询(10ms) → 归还连接(1ms) = 12ms
 * 
 * 核心思想：预先创建一组连接，复用而不是每次都创建/销毁
 */

#ifndef MYSQL_POOL_H
#define MYSQL_POOL_H

#include <string>      // std::string - 字符串
#include <vector>      // std::vector - 动态数组
#include <map>         // std::map - 键值对（存储查询结果）
#include <mutex>       // std::mutex - 互斥锁（线程安全）
#include <memory>      // std::unique_ptr 等智能指针

// =====================================================
// 第一部分：配置结构体
// =====================================================
/**
 * @struct mysql_pool_config
 * @brief MySQL 连接池的配置参数
 * 
 * 就像开店前要准备的信息：
 * - 在哪里开店（host, port）
 * - 谁来管理（user, password）
 * - 仓库在哪里（database）
 * - 准备多少连接（pool_size）
 */
struct mysql_pool_config {
    std::string host = "127.0.0.1";      // MySQL 服务器地址（本地）
    int port = 3306;                      // MySQL 端口号（默认3306）
    std::string user = "root";            // 数据库用户名
    std::string password;                 // 数据库密码
    std::string database;                 // 要使用的数据库名
    int pool_size = 8;                    // 连接池大小（预创建的连接数）
    int timeout_seconds = 10;             // 连接超时时间（秒）
};

// =====================================================
// 第二部分：前向声明
// =====================================================
/**
 * @brief 前向声明
 * 
 * 告诉编译器："MySqlConn 这个类存在，详细信息后面再说"
 * 
 * 为什么需要？
 * - MySqlPool 类在第21行定义
 * - MySqlConn 类在第70行才定义
 * - 如果不前向声明，MySqlPool 就不知道 MySqlConn 是什么类型
 */
class MySqlConn;

// =====================================================
// 第三部分：MySqlPool 连接池类
// =====================================================
/**
 * @class MySqlPool
 * @brief MySQL 连接池管理器
 * 
 * 架构图：
 * 
 *   ┌─────────────────────────────────────────────┐
 *   │              MySqlPool                      │
 *   ├─────────────────────────────────────────────┤
 *   │                                             │
 *   │  ┌─ 可用队列 available_ ─┐                  │
 *   │  │  [conn#1] [conn#2]    │ → 取出使用      │
 *   │  └───────────────────────┘                  │
 *   │                                             │
 *   │  ┌─ 全部连接 all_conns_ ─┐                  │
 *   │  │  [conn#1] [conn#2]...│ → 析构时清理    │
 *   │  └───────────────────────┘                  │
 *   │                                             │
 *   │  ┌─ Guard (RAII) ───────┐                    │
 *   │  │  自动归还连接        │ → 用完自动释放    │
 *   │  └───────────────────────┘                  │
 *   │                                             │
 *   │  mutex_ ← 保护共享数据                      │
 *   └─────────────────────────────────────────────┘
 */
class MySqlPool {
public:
    /**
     * @brief 构造函数
     * @param config 连接池配置（包含数据库地址、账号、密码等）
     * 
     * 使用 explicit 的原因：
     * - 防止 MySqlPool pool = config; 这样的隐式转换
     * - 必须 MySqlPool pool(config); 显式构造
     */
    explicit MySqlPool(const mysql_pool_config& config);
    
    /**
     * @brief 析构函数
     * 
     * 重要！关闭所有连接，防止资源泄漏
     */
    ~MySqlPool();

    // =====================================================
    // 公共接口
    // =====================================================
    
    /**
     * @brief 初始化连接池
     * @return true 成功，false 失败
     * 
     * 做两件事：
     * 1. 创建 pool_size 个 MySqlConn 对象
     * 2. 每个都调用 connect() 连接数据库
     * 3. 都放入 available_ 可用队列
     */
    bool init();

    // =====================================================
    // Guard 类 - RAII 自动归还机制
    // =====================================================
    /**
     * @class Guard
     * @brief RAII 资源管理类（自动归还连接）
     * 
     * 什么是 RAII？
     * - Resource Acquisition Is Initialization
     * - "资源获取即初始化，资源释放即销毁"
     * 
     * 原理：
     * - 构造函数获取连接
     * - 析构函数自动归还连接
     * - 即使代码抛异常，析构也会执行
     * 
     * 使用示例：
     * @code
     *   {
     *       auto guard = pool.getConn();  // 获取连接
     *       guard->query("SELECT * FROM users");
     *       // guard 超出作用域，自动析构
     *       // 析构函数调用 pool.returnConn(conn)
     *   }
     * @endcode
     */
    class Guard {
    public:
        /**
         * @brief 构造函数 - 获取连接
         * @param pool 连接池指针
         * @param conn 要管理的连接指针
         */
        Guard(MySqlPool* pool, MySqlConn* conn) 
            : pool_(pool), conn_(conn) { 
        }

        /**
         * @brief 析构函数 - 自动归还连接！
         * 
         * 这是 RAII 的核心！
         * 当 Guard 对象被销毁时（比如离开作用域），自动调用 returnConn
         */
        ~Guard() { 
            // 调试日志
            fprintf(stderr, "[DEBUG] Guard析构, conn=%p, pool_=%p\n", 
                    (void*)conn_, (void*)pool_); 
            // 自动归还连接
            if (pool_ && conn_) pool_->returnConn(conn_); 
        }

        /**
         * @brief 获取底层连接指针
         */
        MySqlConn* get() const { return conn_; }

        /**
         * @brief 重载 -> 运算符，方便调用 MySqlConn 的方法
         * 
         * 这样就可以 guard->query() 而不是 guard.get()->query()
         */
        MySqlConn* operator->() const { return conn_; }

    private:
        MySqlPool* pool_;   // 连接池指针（用于归还）
        MySqlConn* conn_;   // 管理的连接指针
    };

    /**
     * @brief 获取一个连接
     * @return Guard 对象（RAII，自动归还）
     * 
     * 获取流程：
     * 1. 先看 available_ 有没有空闲连接
     * 2. 有就取出一个
     * 3. 没有就创建新连接（扩容）
     * 4. 返回 Guard 对象
     */
    Guard getConn();

    // =====================================================
    // 简化操作接口（直接执行 SQL）
    // =====================================================
    // 注意：这些方法内部会自动获取连接、归还连接
    // 适合简单的单次查询
    bool query(const std::string& sql);              // 执行 SELECT 查询
    bool execute(const std::string& sql);            // 执行 INSERT/UPDATE/DELETE
    int getRowCount();                               // 获取结果行数
    int getInsertId();                               // 获取插入的ID
    std::map<std::string, std::string> nextRow();    // 获取下一行数据
    bool next();                                     // 移动到下一行

    // =====================================================
    // 连接池状态查询
    // =====================================================
    size_t getPoolSize() const { 
        return pool_size_;  // 连接池总大小
    }
    
    size_t getActiveCount() const;  // 正在使用的连接数

// =====================================================
// 私有成员
// =====================================================
private:
    /**
     * @brief 分配一个新连接（扩容时调用）
     * @return 新创建的连接，失败返回 nullptr
     */
    MySqlConn* allocate();

    /**
     * @brief 归还连接到可用队列
     * @param conn 要归还的连接
     * 
     * 流程：
     * 1. 加锁（线程安全）
     * 2. 如果可用队列未满，放入队列
     * 3. 如果已满，删除连接（超过池大小）
     */
    void returnConn(MySqlConn* conn);

    // ==================== 私有成员变量 ====================
    
    mysql_pool_config config_;       // 连接池配置（数据库信息）
    size_t pool_size_;               // 连接池大小
    
    /**
     * @brief 可用连接队列
     * 
     * 存储当前空闲的连接
     * - getConn() 时从尾部取出一个
     * - returnConn() 时放回一个
     * 
     * 使用 vector + 后进先出（LIFO）
     * 好处：更好的缓存局部性，刚还回来的连接最热
     */
    std::vector<MySqlConn*> available_;
    
    /**
     * @brief 全部连接的追踪列表
     * 
     * 用于析构时清理所有连接
     * 防止内存泄漏
     */
    std::vector<MySqlConn*> all_conns_;
    
    /**
     * @brief 互斥锁
     * 
     * 保护 available_ 和 all_conns_
     * 保证多线程访问连接池时的线程安全
     */
    mutable std::mutex mutex_;
};

// =====================================================
// 第四部分：MySqlConn 单个连接包装类
// =====================================================
/**
 * @class MySqlConn
 * @brief 对原生 MySQL 连接的封装
 * 
 * 封装了什么？
 * - 原生 MYSQL* 句柄
 * - 查询结果 MYSQL_RES*
 * - 当前行数据
 * 
 * 为什么要封装？
 * - 提供更易用的 C++ 接口
 * - 管理结果集的获取和遍历
 * - 统一错误处理
 */
class MySqlConn {
public:
    /**
     * @brief 友元声明
     * 
     * 让 MySqlPool 可以访问 MySqlConn 的所有私有成员
     * 
     * 为什么需要？
     * - Pool 需要完全控制 Conn 的生命周期
     * - Pool 需要访问 conn_、connected_ 等内部状态
     * - 避免写大量 getter/setter
     */
    friend class MySqlPool;
    
    /**
     * @brief 析构函数
     * 
     * 确保连接被正确关闭
     */
    ~MySqlConn();

    // ==================== 公共接口 ====================
    
    /**
     * @brief 建立数据库连接
     * @param config 数据库配置
     * @return true 成功，false 失败
     * 
     * 流程：
     * 1. mysql_init() 初始化 MYSQL 结构
     * 2. 设置超时选项
     * 3. mysql_real_connect() 建立连接
     * 4. 设置字符集为 utf8mb4
     */
    bool connect(const mysql_pool_config& config);
    
    /**
     * @brief 断开连接
     * 
     * 清理：
     * 1. 释放结果集
     * 2. 关闭 MYSQL 连接
     * 3. 重置状态
     */
    void disconnect();

    /**
     * @brief 执行查询（SELECT）
     * @param sql SQL 语句
     * @return true 成功，false 失败
     */
    bool query(const std::string& sql);
    
    /**
     * @brief 执行命令（INSERT/UPDATE/DELETE）
     * @param sql SQL 语句
     * @return true 成功，false 失败
     */
    bool execute(const std::string& sql);
    
    /**
     * @brief 获取结果行数
     * @return 行数
     */
    int getRowCount();
    
    /**
     * @brief 获取插入操作的自动递增 ID
     * @return 插入的 ID
     */
    int getInsertId();
    
    /**
     * @brief 获取下一行数据
     * @return 键值对形式的行数据（字段名 -> 值）
     * 
     * 示例：
     * @code
     *   query("SELECT id, name FROM users");
     *   while (nextRow().size() > 0) {
     *       std::cout << row["id"] << " " << row["name"];
     *   }
     * @endcode
     */
    std::map<std::string, std::string> nextRow();
    
    /**
     * @brief 移动到下一行（不获取数据）
     * @return true 还有数据，false 已到末尾
     */
    bool next();
    
    /**
     * @brief 获取错误信息
     * @return 错误描述字符串
     */
    std::string getError();

    /**
     * @brief 检查连接状态
     * @return true 已连接，false 未连接
     */
    bool isConnected() const { return connected_; }

// =====================================================
// 私有成员（仅 MySqlPool 可访问）
// =====================================================
private:
    // ==================== MySQL 句柄 ====================
    struct MYSQL* conn_ = nullptr;     // MySQL 原生连接句柄
    struct MYSQL_STMT* stmt_ = nullptr; // 预处理语句句柄（暂未使用）
    
    // ==================== 查询结果 ====================
    struct MYSQL_RES* result_ = nullptr; // 查询结果集
    char** row_ = nullptr;                // 当前行数据（原始数组）
    
    // ==================== 解析后的行数据 ====================
    std::map<std::string, std::string> current_row_; // 当前行（字段名→值）

    // ==================== 连接状态 ====================
    bool connected_ = false;   // 是否已连接
    std::string error_;         // 错误信息
};

#endif // MYSQL_POOL_H
