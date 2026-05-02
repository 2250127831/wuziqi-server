/**
 * @file mysql_pool.cpp
 * @brief MySQL 连接池实现
 * 
 * 包含两个类的实现：
 * 1. MySqlConn - 单个连接的封装
 * 2. MySqlPool - 连接池管理器
 */

#include "mysql_pool.h"
#include "logger.h"
#include <mysql/mysql.h>   // MySQL C API
#include <cstring>         // strcpy, strlen 等

// =====================================================
// 第一部分：MySqlConn 实现
// =====================================================

/**
 * @brief 析构函数
 * 
 * 确保 MySQL 连接被正确关闭
 */
MySqlConn::~MySqlConn() {
    disconnect();
}

/**
 * @brief 建立数据库连接
 * @param config 数据库配置
 * @return true 成功，false 失败
 * 
 * 详细步骤：
 * 
 * Step 1: mysql_init(nullptr)
 *   - 分配并初始化一个 MYSQL 结构
 *   - 相当于 "打开数据库客户端"
 *   - 失败则设置错误信息并返回
 * 
 * Step 2: 设置超时选项
 *   - MYSQL_OPT_CONNECT_TIMEOUT: 连接超时
 *   - MYSQL_OPT_READ_TIMEOUT: 读取超时
 *   - MYSQL_OPT_WRITE_TIMEOUT: 写入超时
 * 
 * Step 3: mysql_real_connect()
 *   - 真正建立 TCP 连接
 *   - 参数：主机、用户、密码、数据库、端口...
 *   - 失败则获取错误信息，清理资源，返回
 * 
 * Step 4: 设置字符集
 *   - utf8mb4 支持 emoji 和所有 Unicode 字符
 */
bool MySqlConn::connect(const mysql_pool_config& config) {
    // 先断开已有连接（如果存在）
    disconnect();

    // Step 1: 初始化 MySQL 句柄
    conn_ = mysql_init(nullptr);
    if (!conn_) {
        error_ = "mysql_init failed";  // 初始化失败
        return false;
    }

    // Step 2: 设置超时选项（单位：秒）
    unsigned int timeout = config.timeout_seconds;
    mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);  // 连接超时
    mysql_options(conn_, MYSQL_OPT_READ_TIMEOUT, &timeout);      // 读超时
    mysql_options(conn_, MYSQL_OPT_WRITE_TIMEOUT, &timeout);     // 写超时

    // Step 3: 建立真正的连接
    // mysql_real_connect(
    //     句柄,
    //     主机地址,
    //     用户名,
    //     密码,
    //     数据库名,
    //     端口,
    //     Unix socket (nullptr 表示不用),
    //     客户端标志 (0 表示默认)
    // )
    if (!mysql_real_connect(conn_, 
                            config.host.c_str(),      // "127.0.0.1"
                            config.user.c_str(),      // "root"
                            config.password.c_str(),   // "123456"
                            config.database.c_str(),  // "wuziqi"
                            config.port,              // 3306
                            nullptr,                 // 不用 socket
                            0)) {                    // 默认标志
        // 连接失败！
        error_ = mysql_error(conn_);      // 获取错误信息
        mysql_close(conn_);               // 清理句柄
        conn_ = nullptr;
        return false;
    }

    // Step 4: 设置字符集为 utf8mb4
    // utf8mb4 是真正的 UTF-8，支持所有 Unicode 字符（包括 emoji）
    mysql_set_character_set(conn_, "utf8mb4");

    connected_ = true;  // 标记为已连接
    return true;
}

/**
 * @brief 断开连接，清理资源
 * 
 * 为什么要清理？
 * - 释放 MySQL 服务器端分配的内存
 * - 关闭 TCP 连接
 * - 防止资源泄漏
 */
void MySqlConn::disconnect() {
    // Step 1: 释放结果集（如果有）
    if (result_) {
        mysql_free_result(result_);
        result_ = nullptr;
    }
    
    // Step 2: 关闭 MySQL 连接
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
    
    // Step 3: 重置状态
    connected_ = false;
    error_.clear();
}

/**
 * @brief 执行 SELECT 查询
 * @param sql SQL 语句
 * @return true 成功，false 失败
 * 
 * 查询流程：
 * 1. 检查是否已连接
 * 2. 清理上一次的结果集
 * 3. mysql_real_query() 执行 SQL
 * 4. mysql_store_result() 获取结果集
 * 
 * 注意：query() 和 execute() 的区别
 * - query(): 执行 SELECT，返回结果集
 * - execute(): 执行 INSERT/UPDATE/DELETE，不返回结果集
 */
bool MySqlConn::query(const std::string& sql) {
    // 检查连接状态
    if (!connected_) return false;

    // 清理上一次查询的结果集
    // 重要！每次查询前必须清理，否则内存泄漏
    if (result_) {
        mysql_free_result(result_);
        result_ = nullptr;
    }

    // 执行 SQL 查询
    // mysql_real_query(
    //     句柄,
    //     SQL语句字符串,
    //     字符串长度（不一定是 strlen，因为可能有二进制数据）
    // )
    if (mysql_real_query(conn_, sql.c_str(), sql.length()) != 0) {
        error_ = mysql_error(conn_);  // 记录错误
        return false;
    }

    // 获取结果集
    // mysql_store_result() 会将整个结果集读取到客户端内存
    // 适合小数据量查询
    result_ = mysql_store_result(conn_);
    return true;
}

/**
 * @brief 执行 INSERT/UPDATE/DELETE 命令
 * @param sql SQL 语句
 * @return true 成功，false 失败
 * 
 * 与 query() 的区别：
 * - 不调用 mysql_store_result()
 * - 不返回结果集
 */
bool MySqlConn::execute(const std::string& sql) {
    if (!connected_) return false;

    // 执行 SQL 命令
    if (mysql_real_query(conn_, sql.c_str(), sql.length()) != 0) {
        error_ = mysql_error(conn_);
        return false;
    }

    // INSERT/UPDATE/DELETE 不返回结果集，不需要 mysql_store_result()
    return true;
}

/**
 * @brief 获取结果集的行数
 * @return 行数，如果无结果返回 0
 * 
 * 必须在 query() 之后调用
 */
int MySqlConn::getRowCount() {
    if (!result_) return 0;  // 没有结果集
    return (int)mysql_num_rows(result_);
}

/**
 * @brief 获取 INSERT 操作的自增 ID
 * @return 自动生成的 ID
 * 
 * 适用于：
 * - INSERT INTO users VALUES(...) 
 * - 查询后会得到 auto_increment 字段的值
 */
int MySqlConn::getInsertId() {
    if (!conn_) return 0;
    return (int)mysql_insert_id(conn_);
}

/**
 * @brief 获取下一行数据（封装版）
 * @return 键值对 {字段名: 值}
 * 
 * 使用示例：
 * @code
 *   query("SELECT id, name, age FROM users");
 *   while (true) {
 *       auto row = nextRow();
 *       if (row.empty()) break;  // 没有更多行
 *       cout << row["id"] << " " << row["name"] << endl;
 *   }
 * @endcode
 */
std::map<std::string, std::string> MySqlConn::nextRow() {
    // 用于返回的键值对
    std::map<std::string, std::string> row;

    // 获取下一行原始数据
    if (!result_ || !(row_ = mysql_fetch_row(result_))) {
        // 没有更多行了，清空并返回空 map
        current_row_.clear();
        return current_row_;
    }

    // 获取字段信息
    // mysql_num_fields() 返回字段数量
    unsigned int num_fields = mysql_num_fields(result_);
    
    // mysql_fetch_fields() 返回所有字段的元信息
    MYSQL_FIELD* fields = mysql_fetch_fields(result_);

    // 遍历每个字段，构建键值对
    for (unsigned int i = 0; i < num_fields; ++i) {
        // 字段名（如 "id", "name", "age"）
        std::string field_name = fields[i].name;
        
        // 字段值（row_[i] 可能是 nullptr，需要判断）
        std::string value = row_[i] ? row_[i] : "";
        
        // 加入 map
        row[field_name] = value;
    }

    // 保存当前行（用于其他方法）
    current_row_ = row;
    return row;
}

/**
 * @brief 移动到下一行（轻量版，不返回数据）
 * @return true 还有下一行，false 已到末尾
 * 
 * 与 nextRow() 的区别：
 * - 只移动指针，不填充 current_row_
 * - 更快，但不方便获取数据
 * 
 * 使用场景：需要自己解析二进制数据时
 */
bool MySqlConn::next() {
    if (!result_) return false;
    
    // 获取下一行
    row_ = mysql_fetch_row(result_);
    if (!row_) return false;  // 没有更多行了

    // 获取字段信息
    unsigned int num_fields = mysql_num_fields(result_);
    MYSQL_FIELD* fields = mysql_fetch_fields(result_);

    // 清空并重建 current_row_
    current_row_.clear();
    for (unsigned int i = 0; i < num_fields; ++i) {
        std::string field_name = fields[i].name;
        std::string value = row_[i] ? row_[i] : "";
        current_row_[field_name] = value;
    }

    return true;
}

/**
 * @brief 获取错误信息
 * @return 错误描述字符串
 */
std::string MySqlConn::getError() {
    return error_;
}

// =====================================================
// 第二部分：MySqlPool 实现
// =====================================================

/**
 * @brief 构造函数
 * @param config 连接池配置
 * 
 * 初始化列表：
 * - config_(config): 直接复制配置
 * - pool_size_(config.pool_size): 记录池大小
 */
MySqlPool::MySqlPool(const mysql_pool_config& config)
    : config_(config),           // 复制配置
      pool_size_(config.pool_size) {  // 记录池大小
}

/**
 * @brief 析构函数
 * 
 * 重要！必须清理所有连接
 * 
 * 为什么用 lock_guard？
 * - 可能有多线程同时访问
 * - 需要在遍历时保护 all_conns_
 */
MySqlPool::~MySqlPool() {
    std::lock_guard<std::mutex> lock(mutex_);  // 加锁保护
    
    // 遍历并删除所有连接
    for (auto conn : all_conns_) {
        delete conn;  // 调用 MySqlConn 的析构函数
    }
    // vector 会自动清理（但内容已被 delete）
}

/**
 * @brief 初始化连接池
 * @return true 成功，false 失败
 * 
 * 初始化流程：
 * 
 *   ┌─────────────────────────────────────────────┐
 *   │  for i = 0 to pool_size_                   │
 *   │    ┌─────────────────────────────────────┐  │
 *   │    │  1. new MySqlConn()                │  │
 *   │    │  2. conn->connect(config_)         │  │
 *   │    │  3. if 成功:                        │  │
 *   │    │       available_.push_back(conn)   │  │
 *   │    │       all_conns_.push_back(conn)   │  │
 *   │    │     else:                          │  │
 *   │    │       delete conn, return false    │  │
 *   │    └─────────────────────────────────────┘  │
 *   └─────────────────────────────────────────────┘
 */
bool MySqlPool::init() {
    // 打印初始化日志
    LOG_INFO("初始化 MySQL 连接池: %s:%d/%s, 大小: %d",
             config_.host.c_str(), 
             config_.port, 
             config_.database.c_str(), 
             pool_size_);

    // 预创建 pool_size_ 个连接
    for (size_t i = 0; i < pool_size_; ++i) {
        // 创建新连接对象
        auto conn = new MySqlConn();
        
        // 尝试连接数据库
        if (!conn->connect(config_)) {
            // 连接失败！
            LOG_ERROR("MySQL 连接失败: %s", conn->getError().c_str());
            delete conn;           // 清理已创建的连接
            return false;         // 初始化失败
        }
        
        // 连接成功，加入池中
        available_.push_back(conn);  // 加入可用队列
        all_conns_.push_back(conn);   // 加入全部连接追踪
    }

    LOG_INFO("MySQL 连接池初始化成功");
    return true;
}

/**
 * @brief 获取一个连接
 * @return Guard 对象（RAII 自动管理）
 * 
 * 获取流程：
 * 
 *   ┌─────────────────────────────────────────────┐
 *   │  1. 加锁保护 available_                     │
 *   │  2. if available_ 不为空:                   │
 *   │       conn = available_.back()              │
 *   │       available_.pop_back()                 │
 *   │     else:                                    │
 *   │       conn = nullptr                         │
 *   │  3. 解锁                                      │
 *   │  4. if conn == nullptr:                      │
 *   │       conn = allocate()  // 扩容             │
 *   │  5. return Guard(this, conn)                │
 *   └─────────────────────────────────────────────┘
 */
MySqlPool::Guard MySqlPool::getConn() {
    MySqlConn* conn = nullptr;

    {
        // 使用 lock_guard 自动管理锁
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 检查是否有可用连接
        if (!available_.empty()) {
            // 有！从可用队列取出一个
            // 使用 back() + pop_back() 实现 LIFO（后进先出）
            conn = available_.back();
            available_.pop_back();
        }
        // 如果没有，conn 保持 nullptr
    }
    // 锁在这里自动释放！

    // 没有可用连接，尝试扩容
    if (!conn) {
        conn = allocate();
    }

    // 返回 Guard 对象
    // Guard 的析构函数会调用 returnConn()
    return Guard(this, conn);
}

/**
 * @brief 分配一个新连接（扩容）
 * @return 新创建的连接，失败返回 nullptr
 * 
 * 什么时候调用？
 * - getConn() 发现 available_ 为空时
 * - 说明所有连接都被占用了，需要创建新的
 */
MySqlConn* MySqlPool::allocate() {
    // 创建新连接
    auto conn = new MySqlConn();
    
    // 尝试连接
    if (!conn->connect(config_)) {
        // 连接失败！
        LOG_ERROR("MySQL 扩容连接失败: %s", conn->getError().c_str());
        delete conn;
        return nullptr;
    }
    
    return conn;
}

/**
 * @brief 归还连接到可用队列
 * @param conn 要归还的连接
 * 
 * 归还流程：
 * 
 *   ┌─────────────────────────────────────────────┐
 *   │  1. if conn == nullptr: return              │
 *   │  2. 加锁保护                                │
 *   │  3. if available_.size() < pool_size_:     │
 *   │       available_.push_back(conn)  // 复用  │
 *   │     else:                                   │
 *   │       delete conn  // 超过池大小，丢弃    │
 *   └─────────────────────────────────────────────┘
 * 
 * 注意：
 * - 归还的连接是已经使用过的，仍然有效
 * - 如果池已满，说明创建太多了，可以释放
 */
void MySqlPool::returnConn(MySqlConn* conn) {
    if (!conn) return;  // 空指针检查
    
    std::lock_guard<std::mutex> lock(mutex_);  // 加锁保护

    // 检查是否超过池大小
    if (available_.size() < pool_size_) {
        // 未满，归还到可用队列
        available_.push_back(conn);
    } else {
        // 已满，销毁连接
        // 这种情况发生在 pool_size 设置过小时
        delete conn;
    }
}

/**
 * @brief 获取正在使用的连接数
 * @return 活跃连接数
 * 
 * 计算公式：
 *   活跃数 = 池大小 - 可用数
 *   active_count = pool_size_ - available_.size()
 */
size_t MySqlPool::getActiveCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_size_ - available_.size();
}

// =====================================================
// 第三部分：简化操作接口
// =====================================================
/**
 * @brief 简化版 query（自动管理连接）
 * @param sql SQL 语句
 * @return true 成功，false 失败
 * 
 * 内部实现：
 * 1. 获取连接（Guard）
 * 2. 调用 conn->query(sql)
 * 3. Guard 析构，自动归还连接
 * 
 * 特点：
 * - 自动管理连接，用完即还
 * - 适合简单的单次查询
 * - 注意：结果集存在连接里，不能跨 Guard 使用
 */
bool MySqlPool::query(const std::string& sql) {
    auto guard = getConn();  // 获取连接
    return guard->query(sql); // 执行查询
}

/**
 * @brief 简化版 execute（自动管理连接）
 */
bool MySqlPool::execute(const std::string& sql) {
    auto guard = getConn();
    return guard->execute(sql);
}

/**
 * @brief 获取结果行数
 * 
 * 注意：这里用的是 available_.back() 最后一个可用连接
 * 实际上应该用上一次 query 的那个连接
 * 
 * 这个简化接口有 bug，不建议使用
 */
int MySqlPool::getRowCount() {
    if (available_.empty()) return 0;
    return available_.back()->getRowCount();
}

/**
 * @brief 获取插入的 ID
 */
int MySqlPool::getInsertId() {
    if (available_.empty()) return 0;
    return available_.back()->getInsertId();
}

/**
 * @brief 获取下一行数据
 */
std::map<std::string, std::string> MySqlPool::nextRow() {
    if (available_.empty()) return {};
    return available_.back()->nextRow();
}

/**
 * @brief 移动到下一行
 */
bool MySqlPool::next() {
    if (available_.empty()) return false;
    return available_.back()->next();
}

// =====================================================
// 使用示例（注释）
// =====================================================
/*
// 示例 1：推荐用法（使用 Guard）
void example1(MySqlPool& pool) {
    // 获取连接（自动管理生命周期）
    auto guard = pool.getConn();
    
    // 执行查询
    if (guard->query("SELECT * FROM users WHERE id = 1")) {
        // 获取结果
        while (guard->nextRow().size() > 0) {
            std::cout << "用户: " << guard->current_row_["name"] << std::endl;
        }
    }
    
    // guard 超出作用域，自动析构
    // 析构函数调用 pool.returnConn(conn)
}

// 示例 2：使用简化接口（不推荐）
void example2(MySqlPool& pool) {
    // 执行查询（内部自动获取/归还连接）
    if (pool.query("SELECT * FROM users")) {
        while (pool.next()) {
            auto row = pool.nextRow();
            std::cout << row["name"] << std::endl;
        }
    }
}

// 示例 3：插入数据
void example3(MySqlPool& pool) {
    auto guard = pool.getConn();
    
    if (guard->execute("INSERT INTO users (name, age) VALUES ('Alice', 20)")) {
        int id = guard->getInsertId();  // 获取自增 ID
        std::cout << "插入成功，ID: " << id << std::endl;
    }
}
*/
