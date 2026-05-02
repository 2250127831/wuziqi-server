/**
 * @file handler.h
 * @brief 游戏服务处理器
 * 
 * =====================================================
 * 什么是游戏服务？
 * =====================================================
 * 
 * 游戏服务是五子棋游戏的核心：
 * 1. 管理游戏房间
 * 2. 处理游戏逻辑（落子、胜负判定）
 * 3. 管理游戏状态
 * 4. 与玩家实时通信
 * 
 * =====================================================
 * 五子棋规则
 * =====================================================
 * 
 * - 15×15 的棋盘
 * - 黑白双方轮流落子
 * - 先在横/竖/斜方向连成 5 子获胜
 * - 棋盘位置：(0,0) 左上角，(14,14) 右下角
 * 
 * =====================================================
 * 整体架构
 * =====================================================
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │                   Game Service                           │
 *   ├─────────────────────────────────────────────────────────┤
 *   │                                                         │
 *   │  ┌─────────────────────────────────────────────────┐   │
 *   │  │  房间管理 (rooms_)                              │   │
 *   │  │  room_id → GameRoom                             │   │
 *   │  └─────────────────────────────────────────────────┘   │
 *   │                                                         │
 *   │  玩家操作流程:                                        │
 *   │                                                         │
 *   │  ┌─────────────────────────────────────────────────┐   │
 *   │  │  1. 加入房间                                    │   │
 *   │  │     POST /api/room/join                        │   │
 *   │  │     或 WebSocket 认证                         │   │
 *   │  │                                                 │   │
 *   │  │  2. WebSocket 发送落子                        │   │
 *   │  │     WS /ws/game                               │   │
 *   │  │     message: {type:"move", x:7, y:7}          │   │
 *   │  │                                                 │   │
 *   │  │  3. 服务端处理并广播                           │   │
 *   │  │     broadcast: {type:"move", player, x, y}    │   │
 *   │  │                                                 │   │
 *   │  │  4. 判断胜负                                    │   │
 *   │  │     达到 5 连子获胜                            │   │
 *   │  │                                                 │   │
 *   │  │  5. 游戏结束                                    │   │
 *   │  │     broadcast: {type:"game_over", winner}     │   │
 *   │  └─────────────────────────────────────────────────┘   │
 *   │                                                         │
 *   └─────────────────────────────────────────────────────────┘
 */

#ifndef GAME_SERVICE_HANDLER_H
#define GAME_SERVICE_HANDLER_H

#include <string>          // std::string
#include <memory>         // std::shared_ptr
#include <map>           // std::map
#include <vector>          // std::vector

// 引入公共模块
#include "connection_mgr.h" // 连接管理器
#include "logger.h"       // 日志系统
#include "utils.h"        // 工具函数

// 前向声明
class GameRoom;

/**
 * @class GameServiceHandler
 * @brief 游戏服务处理器
 * 
 * 处理游戏相关的请求
 */
class GameServiceHandler {
public:
    /**
     * @brief 构造函数
     */
    GameServiceHandler();
    
    /**
     * @brief 析构函数
     */
    ~GameServiceHandler();

    /**
     * @brief 初始化
     */
    bool init();

    // =====================================================
    // HTTP 接口
    // =====================================================
    
    /**
     * @brief 创建房间
     * @param req JSON 请求
     * @return JSON 响应
     * 
     * 被 match-service 调用
     */
    std::string handleCreateRoom(const std::string& req);
    
    /**
     * @brief 加入房间
     * @param room_id 房间 ID
     * @param user_id 用户 ID
     * @return JSON 响应
     */
    std::string handleJoinRoom(const std::string& room_id, int user_id);
    
    /**
     * @brief 获取房间信息
     * @param room_id 房间 ID
     * @return JSON 响应
     */
    std::string handleGetRoom(const std::string& room_id);

    // =====================================================
    // WebSocket 消息处理
    // =====================================================
    
    /**
     * @brief 处理 WebSocket 消息
     * @param fd 文件描述符
     * @param user_id 用户 ID
     * @param message 消息内容（JSON）
     * 
     * 消息类型：
     * - move: 落子
     * - chat: 聊天
     * - surrender: 投降
     * - ready: 准备
     */
    void handleWebSocketMessage(int fd, int user_id, const std::string& message);

// =====================================================
// 私有成员
// =====================================================
private:
    /**
     * @brief 处理落子
     */
    void handleMove(int fd, int user_id, int x, int y);

    /**
     * @brief 处理投降
     */
    void handleSurrender(int fd, int user_id);

    /**
     * @brief 处理准备
     */
    void handleReady(int fd, int user_id);

    /**
     * @brief 处理聊天
     */
    void handleChat(int fd, int user_id, const std::string& message);

    /**
     * @brief 获取或创建房间
     */
    std::shared_ptr<GameRoom> getOrCreateRoom(const std::string& room_id);

    /**
     * @brief 获取用户的房间
     */
    std::shared_ptr<GameRoom> getUserRoom(int user_id);

    /**
     * @brief 广播消息
     */
    void broadcastToRoom(const std::string& room_id, const std::string& message, int exclude_fd);

    /**
     * @brief 发送消息给用户
     */
    void sendToUser(int user_id, const std::string& message);

    // ==================== 成员变量 ====================
    
    /**
     * @brief 房间映射
     */
    std::map<std::string, std::shared_ptr<GameRoom>> rooms_;
    
    /**
     * @brief 保护 rooms_ 的锁
     */
    std::mutex rooms_mutex_;
};

/**
 * @class GameRoom
 * @brief 游戏房间
 * 
 * 包含：
 * - 房间 ID
 * - 两个玩家
 * - 棋盘状态
 * - 当前回合
 * - 游戏状态
 */
class GameRoom {
public:
    std::string room_id;              // 房间 ID
    int player1_id = 0;              // 玩家 1（黑方）
    int player2_id = 0;              // 玩家 2（白方）
    int player1_fd = -1;             // 玩家 1 的 fd
    int player2_fd = -1;             // 玩家 2 的 fd
    
    // 棋盘：15×15，0=空，1=黑，2=白
    int board[15][15];
    
    int current_player = 1;         // 当前回合（1 或 2）
    bool player1_ready = false;      // 玩家 1 是否准备
    bool player2_ready = false;      // 玩家 2 是否准备
    
    bool game_started = false;       // 游戏是否已开始
    bool game_over = false;         // 游戏是否已结束
    int winner = 0;                  // 获胜者（1 或 2），0 表示平局
    
    long long start_time = 0;        // 游戏开始时间
    long long end_time = 0;         // 游戏结束时间
    
    // 构造函数
    GameRoom(const std::string& id) : room_id(id) {
        memset(board, 0, sizeof(board));
    }
    
    /**
     * @brief 添加玩家
     */
    bool addPlayer(int user_id, int fd);
    
    /**
     * @brief 获取对手 ID
     */
    int getOpponentId(int user_id) const;
    
    /**
     * @brief 获取对手 fd
     */
    int getOpponentFd(int user_id) const;
    
    /**
     * @brief 落子
     * @return true 成功
     */
    bool placePiece(int player, int x, int y);
    
    /**
     * @brief 检查是否获胜
     * @return 获胜的玩家（1 或 2），0 表示无获胜者
     */
    int checkWin(int x, int y);
    
    /**
     * @brief 检查棋盘是否已满
     */
    bool isBoardFull() const;
    
    /**
     * @brief 获取房间状态
     */
    std::string getStatus() const;
};

#endif // GAME_SERVICE_HANDLER_H
