/**
 * @file connection_mgr.h
 * @brief 游戏服务连接管理器
 * 
 * =====================================================
 * 什么是连接管理器？
 * =====================================================
 * 
 * 连接管理器负责：
 * 1. 管理所有玩家的 WebSocket 连接
 * 2. 用户 ID 与连接 fd 的映射
 * 3. 用户与游戏房间的映射
 * 4. 消息广播
 * 
 * =====================================================
 * WebSocket 连接是什么？
 * =====================================================
 * 
 * WebSocket 是一种双向通信协议：
 * - HTTP: 请求 → 响应（一问一答）
 * - WebSocket: 建立连接后，可以随时发送消息
 * 
 * 游戏场景：
 * - 玩家移动时，不需要 HTTP 请求
 * - 直接通过 WebSocket 发送位置更新
 * - 服务器也可以主动推送（如其他玩家的移动）
 * 
 * =====================================================
 * 整体架构
 * =====================================================
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │              ConnectionManager                          │
 *   ├─────────────────────────────────────────────────────────┤
 *   │                                                         │
 *   │  ┌─────────────────────────────────────────────────┐   │
 *   │  │  fd_to_conn_: fd → WebSocket 连接               │   │
 *   │  │  fd: 文件描述符（操作系统分配的编号）             │   │
 *   │  └─────────────────────────────────────────────────┘   │
 *   │                                                         │
 *   │  ┌─────────────────────────────────────────────────┐   │
 *   │  │  user_to_fd_: user_id → fd                     │   │
 *   │  │  用于查找某个用户的连接                          │   │
 *   │  └─────────────────────────────────────────────────┘   │
 *   │                                                         │
 *   │  ┌─────────────────────────────────────────────────┐   │
 *   │  │  room_to_users_: room_id → {user_id1,2,...}    │   │
 *   │  │  用于向房间内所有玩家广播                        │   │
 *   │  └─────────────────────────────────────────────────┘   │
 *   │                                                         │
 *   │  ┌─────────────────────────────────────────────────┐   │
 *   │  │  user_to_room_: user_id → room_id              │   │
 *   │  │  用于查找某个用户所在的房间                      │   │
 *   │  └─────────────────────────────────────────────────┘   │
 *   │                                                         │
 *   └─────────────────────────────────────────────────────────┘
 * 
 * 数据流：
 * 
 *   玩家 A ──fd=5──┐
 *                      │
 *   玩家 B ──fd=6──┼── ConnectionManager ──broadcast──→ 房间内的所有玩家
 *                      │
 *   玩家 C ──fd=7──┘
 */

#ifndef GAME_SERVICE_CONNECTION_MGR_H
#define GAME_SERVICE_CONNECTION_MGR_H

#include <string>          // std::string
#include <map>           // std::map
#include <unordered_map>  // std::unordered_map
#include <set>            // std::set
#include <mutex>          // std::mutex
#include <memory>         // std::shared_ptr
#include <atomic>         // std::atomic

// 前向声明
class WsConnection;

/**
 * @class ConnectionManager
 * @brief 连接管理器（单例模式）
 * 
 * 管理所有 WebSocket 连接
 * 
 * 线程安全：
 * - 所有公共方法都会加锁
 * - 使用 std::lock_guard 自动管理锁
 */
class ConnectionManager {
public:
    /**
     * @brief 获取单例实例
     */
    static ConnectionManager& instance() {
        static ConnectionManager mgr;
        return mgr;
    }

    // =====================================================
    // 连接生命周期管理
    // =====================================================
    
    /**
     * @brief 添加新连接
     * @param fd 文件描述符
     * 
     * 当客户端建立 WebSocket 连接时调用
     */
    void addConnection(int fd);
    
    /**
     * @brief 移除连接
     * @param fd 文件描述符
     * 
     * 当连接断开时调用
     * 会清理所有相关映射
     */
    void removeConnection(int fd);

    // =====================================================
    // 用户绑定
    // =====================================================
    
    /**
     * @brief 绑定用户 ID
     * @param fd 文件描述符
     * @param user_id 用户 ID
     * 
     * 当用户登录后调用
     * 会建立 fd ↔ user_id 的映射
     * 
     * 防重复登录：
     * 如果同一用户在其他地方登录，旧连接会被标记为失效
     */
    void bindUser(int fd, int user_id);
    
    /**
     * @brief 根据用户 ID 获取 fd
     * @param user_id 用户 ID
     * @return fd，-1 表示不在线
     */
    int getConnectionFd(int user_id) const;
    
    /**
     * @brief 根据 fd 获取用户 ID
     * @param fd 文件描述符
     * @return user_id，0 表示未登录
     */
    int getUserId(int fd) const;
    
    /**
     * @brief 检查用户是否在线
     */
    bool isUserOnline(int user_id) const;

    // =====================================================
    // 房间管理
    // =====================================================
    
    /**
     * @brief 设置用户所在房间
     * @param user_id 用户 ID
     * @param room_id 房间 ID
     * 
     * 当用户加入房间时调用
     */
    void setUserRoom(int user_id, const std::string& room_id);
    
    /**
     * @brief 移除用户所在房间
     */
    void removeUserRoom(int user_id);
    
    /**
     * @brief 获取用户所在房间
     */
    std::string getUserRoom(int user_id) const;

    // =====================================================
    // 消息发送
    // =====================================================
    
    /**
     * @brief 发送消息给指定用户
     * @param user_id 用户 ID
     * @param message 消息内容
     * @return true 成功
     * 
     * 如果用户不在线，返回 false
     */
    bool sendToUser(int user_id, const std::string& message);
    
    /**
     * @brief 发送消息给指定 fd
     * @param fd 文件描述符
     * @param message 消息内容
     * @return true 成功
     */
    bool sendToFd(int fd, const std::string& message);
    
    /**
     * @brief 向房间内所有用户广播
     * @param room_id 房间 ID
     * @param message 消息内容
     * @param exclude_user_id 排除的用户 ID（可选）
     */
    void broadcastToRoom(const std::string& room_id,
                         const std::string& message,
                         int exclude_user_id = -1);

    // =====================================================
    // 统计
    // =====================================================
    
    /**
     * @brief 获取在线人数
     */
    size_t getOnlineCount() const;

// =====================================================
// 私有成员
// =====================================================
private:
    /** @brief 私有构造函数（单例模式） */
    ConnectionManager() = default;

    // ==================== 连接存储 ====================
    
    /**
     * @brief fd → 连接对象
     */
    std::map<int, std::shared_ptr<WsConnection>> fd_to_conn_;
    
    /**
     * @brief user_id → fd
     */
    std::unordered_map<int, int> user_to_fd_;
    
    /**
     * @brief room_id → 用户集合
     */
    std::unordered_map<std::string, std::set<int>> room_to_users_;
    
    /**
     * @brief user_id → room_id
     */
    std::unordered_map<int, std::string> user_to_room_;
    
    /**
     * @brief 保护所有映射的锁
     */
    mutable std::mutex mutex_;
};

// =====================================================
// WsConnection 类
// =====================================================
/**
 * @class WsConnection
 * @brief WebSocket 连接信息
 */
class WsConnection {
public:
    int fd;                         // 文件描述符
    int user_id = 0;               // 绑定的用户 ID（0 表示未登录）
    bool authenticated = false;    // 是否已认证
    long long connect_time;        // 连接建立时间
    long long last_active_time;    // 最后活跃时间
    
    /**
     * @brief 构造函数
     */
    explicit WsConnection(int _fd) : fd(_fd) {
        connect_time = time(nullptr);
        last_active_time = connect_time;
    }
    
    /**
     * @brief 更新活跃时间
     */
    void updateActivity() {
        last_active_time = time(nullptr);
    }
};

#endif // GAME_SERVICE_CONNECTION_MGR_H
