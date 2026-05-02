/**
 * @file handler.h
 * @brief 匹配服务处理器
 * 
 * =====================================================
 * 什么是匹配服务？
 * =====================================================
 * 
 * 匹配服务负责将等待中的玩家配对成游戏房间
 * 
 * 功能：
 * 1. 玩家加入匹配队列
 * 2. 自动匹配对手
 * 3. 通知游戏服务创建房间
 * 4. 管理匹配状态
 * 
 * =====================================================
 * 匹配算法
 * =====================================================
 * 
 * 采用「分桶匹配」算法：
 * 
 *   玩家等级: 1-100 (数值越高越强)
 * 
 *   桶划分:
 *   - 桶 1: 1-20 级
 *   - 桶 2: 21-40 级
 *   - 桶 3: 41-60 级
 *   - 桶 4: 61-80 级
 *   - 桶 5: 81-100 级
 * 
 *   匹配规则:
 *   - 优先同桶匹配
 *   - 等待超时后扩大范围
 *   - 最低 2 人才开始匹配
 * 
 * =====================================================
 * 整体架构
 * =====================================================
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │                  Match Service                          │
 *   ├─────────────────────────────────────────────────────────┤
 *   │
 *   │   匹配队列 (Redis):                                    │
 *   │   ┌─────────────────────────────────────────────────┐   │
 *   │   │  match:queue:{bucket} → List[player_info]     │   │
 *   │   │  match:wait:{player_id} → timestamp          │   │
 *   │   └─────────────────────────────────────────────────┘   │
 *   │                                                         │
 *   │   匹配流程:                                            │
 *   │                                                         │
 *   │   ┌─────────────────────────────────────────────────┐   │
 *   │   │  1. 玩家请求匹配                                │   │
 *   │   │     POST /api/match                            │   │
 *   │   │                                                 │   │
 *   │   │  2. 加入匹配队列                                │   │
 *   │   │     RPUSH match:queue:{bucket} player_info     │   │
 *   │   │                                                 │   │
 *   │   │  3. 轮询检查是否匹配成功                         │   │
 *   │   │     GET /api/match/status                      │   │
 *   │   │                                                 │   │
 *   │   │  4. 匹配成功                                    │   │
 *   │   │     - 通知玩家 room_id                          │   │
 *   │   │     - 通知 game-service 创建房间                │   │
 *   │   └─────────────────────────────────────────────────┘   │
 *   │                                                         │
 *   └─────────────────────────────────────────────────────────┘
 */

#ifndef MATCH_SERVICE_HANDLER_H
#define MATCH_SERVICE_HANDLER_H

#include <string>          // std::string
#include <memory>         // std::shared_ptr
#include <vector>          // std::vector
#include <map>             // std::map
#include <unordered_map>  // std::unordered_map

// 引入公共模块
#include "redis_pool.h"   // Redis 连接池
#include "thread_pool.h"  // 线程池
#include "logger.h"       // 日志系统
#include "http_client.h"  // HTTP 客户端（通知游戏服务）
#include "utils.h"        // 工具函数

/**
 * @struct PlayerInfo
 * @brief 玩家信息
 */
struct PlayerInfo {
    int user_id;              // 用户 ID
    std::string username;     // 用户名
    int level;                // 等级（1-100）
    long long timestamp;      // 加入匹配队列的时间
    int wait_seconds;         // 已等待时间（秒）
};

/**
 * @class MatchServiceHandler
 * @brief 匹配服务处理器
 * 
 * 使用 Redis List 实现匹配队列
 * 
 * 队列结构：
 *   match:queue:{bucket} = [player1, player2, ...]
 * 
 * 每个 player 是 JSON 字符串：
 *   {"user_id": 1, "username": "alice", "level": 50}
 */
class MatchServiceHandler {
public:
    /**
     * @brief 构造函数
     * @param redis Redis 连接池
     * @param pool 线程池
     * @param game_service_url 游戏服务地址
     */
    MatchServiceHandler(std::shared_ptr<RedisPool> redis,
                      std::shared_ptr<ThreadPool> pool,
                      const std::string& game_service_url);
    
    ~MatchServiceHandler() = default;

    // =====================================================
    // 初始化
    // =====================================================
    /**
     * @brief 初始化
     */
    bool init();

    // =====================================================
    // 匹配操作
    // =====================================================
    
    /**
     * @brief 处理加入匹配请求
     * @param user_id 用户 ID
     * @param username 用户名
     * @param level 等级
     * @return JSON 响应
     * 
     * 流程：
     * 
     *   ┌─────────────────────────────────────────────────┐
     *   │  1. 计算桶编号                                  │
     *   │     bucket = (level - 1) / 20 + 1              │
     *   │     level=50 → bucket=3                        │
     *   │                                                 │
     *   │  2. 检查是否已在队列中                           │
     *   │     GET match:wait:{user_id}                   │
     *   │                                                 │
     *   │  3. 加入匹配队列                                │
     *   │     RPUSH match:queue:{bucket} player_json    │
     *   │                                                 │
     *   │  4. 记录等待状态                                │
     *   │     SET match:wait:{user_id} timestamp        │
     *   │                                                 │
     *   │  5. 尝试立即匹配                                │
     *   │     - 尝试匹配同桶玩家                          │
     *   │     - 匹配成功返回 room_id                      │
     *   │     - 未匹配返回等待状态                        │
     *   └─────────────────────────────────────────────────┘
     */
    std::string handleJoinMatch(int user_id, const std::string& username, int level);
    
    /**
     * @brief 处理离开匹配请求
     * @param user_id 用户 ID
     * @return JSON 响应
     */
    std::string handleLeaveMatch(int user_id);
    
    /**
     * @brief 查询匹配状态
     * @param user_id 用户 ID
     * @return JSON 响应
     * 
     * 返回当前状态：
     * - waiting: 等待中
     * - matched: 已匹配成功
     * - not_in_queue: 不在队列中
     */
    std::string handleGetMatchStatus(int user_id);
    
    /**
     * @brief 获取匹配结果（轮询用）
     * @param user_id 用户 ID
     * @return JSON 响应
     * 
     * 如果匹配成功，返回 room_id
     */
    std::string handleGetMatchResult(int user_id);

// =====================================================
// 私有成员
// =====================================================
private:
    /**
     * @brief 计算桶编号
     * @param level 等级
     * @return 桶编号（1-5）
     */
    int calculateBucket(int level);

    /**
     * @brief 尝试匹配
     * @param bucket 桶编号
     * @return 匹配结果（两个玩家信息），空表示未匹配
     */
    std::pair<PlayerInfo, PlayerInfo> tryMatch(int bucket);

    /**
     * @brief 通知游戏服务创建房间
     * @param player1 玩家 1
     * @param player2 玩家 2
     * @return room_id
     */
    std::string notifyGameService(const PlayerInfo& player1, const PlayerInfo& player2);

    /**
     * @brief 检查超时并扩大匹配范围
     * @param bucket 当前桶编号
     * @param player 玩家信息
     * @return 扩大后的桶编号，-1 表示不需要扩大
     */
    int checkTimeoutAndExpand(int bucket, const PlayerInfo& player);

    // ==================== 成员变量 ====================
    
    std::shared_ptr<RedisPool> redis_;       // Redis 连接池
    std::shared_ptr<ThreadPool> pool_;       // 线程池
    std::shared_ptr<HttpClient> http_;       // HTTP 客户端
    std::string game_service_url_;            // 游戏服务地址

    // ==================== 配置常量 ====================
    
    /** @brief 桶大小（每桶包含多少等级） */
    const int bucket_size_ = 20;
    
    /** @brief 等待超时时间（秒），超时后扩大匹配范围 */
    const int wait_timeout_ = 30;
    
    /** @brief 最大等待时间（秒），超时后强制匹配 */
    const int max_wait_time_ = 60;
    
    /** @brief 匹配超时时间（秒） */
    const int match_timeout_ = 300;
};

#endif // MATCH_SERVICE_HANDLER_H
