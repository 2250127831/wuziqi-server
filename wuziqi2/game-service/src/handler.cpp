/**
 * @file handler.cpp
 * @brief 游戏服务处理器实现
 * 
 * =====================================================
 * 核心功能
 * =====================================================
 * 
 * 1. 创建房间（match-service 调用）
 * 2. 加入房间（玩家调用）
 * 3. WebSocket 消息处理（游戏逻辑核心）
 * 
 * =====================================================
 * WebSocket 消息流程
 * =====================================================
 * 
 *   玩家 A ──WebSocket──┐
 *                           │
 *   匹配服务 ──HTTP──┐      │
 *                   │      │
 *   玩家 B ──WebSocket──┼──► GameServiceHandler
 *                           │      │
 *                           │      ├─► 验证
 *                           │      ├─► 更新棋盘
 *                           │      ├─► 检查胜负
 *                           │      └─► 广播
 *                           │
 *                           └─► 玩家 A ◄── WebSocket ──┤
 *                               玩家 B ◄───────────────┘
 */

#include "handler.h"
#include "redis_pool.h"
#include "http_client.h"
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <sys/socket.h>

// =============================================================================
// GameRoom 成员函数实现
// =============================================================================

/**
 * 添加玩家到房间
 */
bool GameRoom::addPlayer(int user_id, int fd) {
    if (player1_id == 0) {
        // 第一个空位，放入玩家 1（黑方）
        player1_id = user_id;
        player1_fd = fd;
        return true;
    } else if (player2_id == 0) {
        // 第二个空位，放入玩家 2（白方）
        player2_id = user_id;
        player2_fd = fd;
        return true;
    }
    // 房间已满
    return false;
}

/**
 * 获取对手 ID
 */
int GameRoom::getOpponentId(int user_id) const {
    if (user_id == player1_id) return player2_id;
    if (user_id == player2_id) return player1_id;
    return 0;
}

/**
 * 获取对手 fd
 */
int GameRoom::getOpponentFd(int user_id) const {
    if (user_id == player1_id) return player2_fd;
    if (user_id == player2_id) return player1_fd;
    return -1;
}

/**
 * 落子
 */
bool GameRoom::placePiece(int player, int x, int y) {
    // 边界检查
    if (x < 0 || x >= 15 || y < 0 || y >= 15) {
        LOG_WARN("落子越界: x=%d, y=%d", x, y);
        return false;
    }
    
    // 位置检查（必须为空）
    if (board[x][y] != 0) {
        LOG_WARN("位置已有棋子: x=%d, y=%d", x, y);
        return false;
    }
    
    // 回合检查
    if ((player == 1 && current_player != 1) ||
        (player == 2 && current_player != 2)) {
        LOG_WARN("不是你的回合: player=%d, current=%d", player, current_player);
        return false;
    }
    
    // 放置棋子
    board[x][y] = player;
    LOG_INFO("落子成功: player=%d, x=%d, y=%d", player, x, y);
    
    return true;
}

/**
 * 检查获胜
 * 
 * 从落子位置向 4 个方向检查：
 * 
 * 方向 1: ─────►  水平
 * 方向 2:  │     垂直
 *          ▼
 * 方向 3:  \     主对角线
 * 方向 4:   \   副对角线
 *            \
 */
int GameRoom::checkWin(int x, int y) {
    int player = board[x][y];
    if (player == 0) return 0;
    
    // 4 个方向的增量：(dx, dy)
    const int dirs[4][2] = {
        {1, 0},   // 水平 →
        {0, 1},   // 垂直 ↓
        {1, 1},   // 主对角线 ↘
        {1, -1}   // 副对角线 ↗
    };
    
    for (int d = 0; d < 4; d++) {
        int dx = dirs[d][0];
        int dy = dirs[d][1];
        
        int count = 1;  // 自身
        
        // 正方向检查
        for (int i = 1; i < 5; i++) {
            int nx = x + dx * i;
            int ny = y + dy * i;
            if (nx >= 0 && nx < 15 && ny >= 0 && ny < 15 && board[nx][ny] == player) {
                count++;
            } else {
                break;
            }
        }
        
        // 反方向检查
        for (int i = 1; i < 5; i++) {
            int nx = x - dx * i;
            int ny = y - dy * i;
            if (nx >= 0 && nx < 15 && ny >= 0 && ny < 15 && board[nx][ny] == player) {
                count++;
            } else {
                break;
            }
        }
        
        // 连成 5 子获胜
        if (count >= 5) {
            return player;
        }
    }
    
    return 0;
}

/**
 * 检查棋盘是否已满
 */
bool GameRoom::isBoardFull() const {
    for (int i = 0; i < 15; i++) {
        for (int j = 0; j < 15; j++) {
            if (board[i][j] == 0) {
                return false;
            }
        }
    }
    return true;
}

/**
 * 获取房间状态（JSON）
 */
std::string GameRoom::getStatus() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"room_id\":\"" << room_id << "\",";
    oss << "\"player1_id\":" << player1_id << ",";
    oss << "\"player2_id\":" << player2_id << ",";
    oss << "\"player1_ready\":" << (player1_ready ? "true" : "false") << ",";
    oss << "\"player2_ready\":" << (player2_ready ? "true" : "false") << ",";
    oss << "\"current_player\":" << current_player << ",";
    oss << "\"game_started\":" << (game_started ? "true" : "false") << ",";
    oss << "\"game_over\":" << (game_over ? "true" : "false");
    oss << "}";
    return oss.str();
}

// =============================================================================
// GameServiceHandler 实现
// =============================================================================

GameServiceHandler::GameServiceHandler() {
    LOG_INFO("GameServiceHandler 初始化");
}

GameServiceHandler::~GameServiceHandler() {
    LOG_INFO("GameServiceHandler 销毁");
}

/**
 * 初始化
 */
bool GameServiceHandler::init() {
    LOG_INFO("游戏服务初始化");
    return true;
}

// =============================================================================
// HTTP 接口实现
// =============================================================================

/**
 * 创建房间
 * 
 * match-service 调用此接口创建游戏房间
 */
std::string GameServiceHandler::handleCreateRoom(const std::string& req) {
    LOG_INFO("创建房间请求: %s", req.c_str());
    
    // 解析请求 JSON
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(req, root)) {
        return "{\"success\":false,\"error\":\"Invalid JSON\"}";
    }
    
    std::string room_id = root["room_id"].asString();
    int player1_id = root["player1_id"].asInt();
    int player2_id = root["player2_id"].asInt();
    
    if (room_id.empty() || player1_id == 0 || player2_id == 0) {
        return "{\"success\":false,\"error\":\"Missing parameters\"}";
    }
    
    // 创建房间
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    auto room = std::make_shared<GameRoom>(room_id);
    
    // 获取玩家 fd
    int fd1 = ConnectionManager::instance().getConnectionFd(player1_id);
    int fd2 = ConnectionManager::instance().getConnectionFd(player2_id);
    
    // 添加玩家
    if (!room->addPlayer(player1_id, fd1)) {
        return "{\"success\":false,\"error\":\"Room is full\"}";
    }
    if (!room->addPlayer(player2_id, fd2)) {
        return "{\"success\":false,\"error\":\"Room is full\"}";
    }
    
    // 保存房间
    rooms_[room_id] = room;
    
    // 设置用户房间映射
    ConnectionManager::instance().setUserRoom(player1_id, room_id);
    ConnectionManager::instance().setUserRoom(player2_id, room_id);
    
    LOG_INFO("房间创建成功: %s, 玩家1=%d, 玩家2=%d", room_id.c_str(), player1_id, player2_id);
    
    // 通知玩家
    Json::Value notify;
    notify["type"] = "game_start";
    notify["room_id"] = room_id;
    notify["your_side"] = 1;  // 黑方先手
    notify["opponent_id"] = player2_id;
    
    Json::FastWriter writer;
    std::string msg = writer.write(notify);
    
    ConnectionManager::instance().sendToUser(player1_id, msg);
    notify["your_side"] = 2;
    notify["opponent_id"] = player1_id;
    msg = writer.write(notify);
    ConnectionManager::instance().sendToUser(player2_id, msg);
    
    return "{\"success\":true}";
}

/**
 * 加入房间
 */
std::string GameServiceHandler::handleJoinRoom(const std::string& room_id, int user_id) {
    LOG_INFO("加入房间请求: room_id=%s, user_id=%d", room_id.c_str(), user_id);
    
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return "{\"success\":false,\"error\":\"Room not found\"}";
    }
    
    auto room = it->second;
    
    // 房间已满
    if (room->player1_id != 0 && room->player2_id != 0) {
        return "{\"success\":false,\"error\":\"Room is full\"}";
    }
    
    // 已经是房间成员
    if (room->player1_id == user_id || room->player2_id == user_id) {
        return "{\"success\":true,\"status\":" + room->getStatus() + "}";
    }
    
    // 加入房间
    int fd = ConnectionManager::instance().getConnectionFd(user_id);
    if (!room->addPlayer(user_id, fd)) {
        return "{\"success\":false,\"error\":\"Failed to join\"}";
    }
    
    ConnectionManager::instance().setUserRoom(user_id, room_id);
    
    LOG_INFO("用户 %d 加入房间 %s 成功", user_id, room_id.c_str());
    
    return "{\"success\":true,\"status\":" + room->getStatus() + "}";
}

/**
 * 获取房间信息
 */
std::string GameServiceHandler::handleGetRoom(const std::string& room_id) {
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return "{\"success\":false,\"error\":\"Room not found\"}";
    }
    
    return "{\"success\":true,\"status\":" + it->second->getStatus() + "}";
}

// =============================================================================
// WebSocket 消息处理
// =============================================================================

/**
 * 处理 WebSocket 消息
 */
void GameServiceHandler::handleWebSocketMessage(int fd, int user_id, const std::string& message) {
    LOG_INFO("WebSocket 消息: fd=%d, user_id=%d, msg=%s", fd, user_id, message.c_str());
    
    // 解析 JSON
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(message, root)) {
        LOG_WARN("JSON 解析失败");
        return;
    }
    
    std::string type = root["type"].asString();
    
    if (type == "move") {
        // 落子
        int x = root["x"].asInt();
        int y = root["y"].asInt();
        handleMove(fd, user_id, x, y);
    } 
    else if (type == "surrender") {
        // 投降
        handleSurrender(fd, user_id);
    }
    else if (type == "ready") {
        // 准备
        handleReady(fd, user_id);
    }
    else if (type == "chat") {
        // 聊天
        std::string text = root["message"].asString();
        handleChat(fd, user_id, text);
    }
}

/**
 * 处理落子
 * 
 * 核心游戏逻辑：
 * 1. 验证落子合法性
 * 2. 更新棋盘
 * 3. 广播给对手
 * 4. 检查胜负
 */
void GameServiceHandler::handleMove(int fd, int user_id, int x, int y) {
    auto room = getUserRoom(user_id);
    if (!room) {
        LOG_WARN("用户不在任何房间");
        sendToUser(user_id, "{\"type\":\"error\",\"message\":\"Not in a room\"}");
        return;
    }
    
    if (room->game_over) {
        sendToUser(user_id, "{\"type\":\"error\",\"message\":\"Game is over\"}");
        return;
    }
    
    // 确定玩家编号（1 或 2）
    int player = (user_id == room->player1_id) ? 1 : 2;
    
    // 落子
    if (!room->placePiece(player, x, y)) {
        sendToUser(user_id, "{\"type\":\"error\",\"message\":\"Invalid move\"}");
        return;
    }
    
    // 广播落子给对手
    Json::Value move_notify;
    move_notify["type"] = "move";
    move_notify["player"] = player;
    move_notify["x"] = x;
    move_notify["y"] = y;
    
    Json::FastWriter writer;
    std::string msg = writer.write(move_notify);
    
    int opponent_fd = room->getOpponentFd(user_id);
    if (opponent_fd >= 0) {
        sendToFd(opponent_fd, msg);
    }
    
    // 检查胜负
    int winner = room->checkWin(x, y);
    
    if (winner > 0) {
        // 有人获胜
        room->game_over = true;
        room->winner = winner;
        room->end_time = time(nullptr);
        
        Json::Value win_notify;
        win_notify["type"] = "game_over";
        win_notify["winner"] = winner;
        win_notify["reason"] = "five_in_a_row";
        
        std::string win_msg = writer.write(win_notify);
        broadcastToRoom(room->room_id, win_msg, -1);
        
        LOG_INFO("游戏结束，获胜者: player=%d", winner);
        
        // TODO: 更新排行榜、战绩等
    }
    else if (room->isBoardFull()) {
        // 棋盘下满，平局
        room->game_over = true;
        room->winner = 0;
        room->end_time = time(nullptr);
        
        Json::Value draw_notify;
        draw_notify["type"] = "game_over";
        draw_notify["winner"] = 0;
        draw_notify["reason"] = "draw";
        
        std::string draw_msg = writer.write(draw_notify);
        broadcastToRoom(room->room_id, draw_msg, -1);
        
        LOG_INFO("游戏结束，平局");
    }
    else {
        // 切换回合
        room->current_player = (player == 1) ? 2 : 1;
        
        // 通知轮到谁
        Json::Value turn_notify;
        turn_notify["type"] = "turn";
        turn_notify["player"] = room->current_player;
        
        std::string turn_msg = writer.write(turn_notify);
        broadcastToRoom(room->room_id, turn_msg, -1);
    }
}

/**
 * 处理投降
 */
void GameServiceHandler::handleSurrender(int fd, int user_id) {
    auto room = getUserRoom(user_id);
    if (!room) return;
    
    if (room->game_over) return;
    
    room->game_over = true;
    
    // 对手获胜
    int winner = (user_id == room->player1_id) ? 2 : 1;
    room->winner = winner;
    
    Json::Value notify;
    notify["type"] = "game_over";
    notify["winner"] = winner;
    notify["reason"] = "surrender";
    
    Json::FastWriter writer;
    broadcastToRoom(room->room_id, writer.write(notify), -1);
    
    LOG_INFO("用户 %d 投降，对手 %d 获胜", user_id, winner);
}

/**
 * 处理准备
 */
void GameServiceHandler::handleReady(int fd, int user_id) {
    auto room = getUserRoom(user_id);
    if (!room) return;
    
    if (room->game_started) return;
    
    // 设置准备状态
    if (user_id == room->player1_id) {
        room->player1_ready = true;
    } else if (user_id == room->player2_id) {
        room->player2_ready = true;
    }
    
    // 广播准备状态
    Json::Value notify;
    notify["type"] = "ready";
    notify["user_id"] = user_id;
    
    Json::FastWriter writer;
    broadcastToRoom(room->room_id, writer.write(notify), -1);
    
    // 双方都准备，开始游戏
    if (room->player1_ready && room->player2_ready) {
        room->game_started = true;
        room->start_time = time(nullptr);
        room->current_player = 1;  // 黑方先手
        
        Json::Value start_notify;
        start_notify["type"] = "game_start";
        start_notify["first_player"] = 1;
        
        broadcastToRoom(room->room_id, writer.write(start_notify), -1);
        
        LOG_INFO("游戏开始！房间: %s", room->room_id.c_str());
    }
}

/**
 * 处理聊天
 */
void GameServiceHandler::handleChat(int fd, int user_id, const std::string& text) {
    auto room = getUserRoom(user_id);
    if (!room) return;
    
    Json::Value notify;
    notify["type"] = "chat";
    notify["user_id"] = user_id;
    notify["message"] = text;
    
    Json::FastWriter writer;
    broadcastToRoom(room->room_id, writer.write(notify), user_id);
}

// =============================================================================
// 辅助函数
// =============================================================================

/**
 * 获取用户的房间
 */
std::shared_ptr<GameRoom> GameServiceHandler::getUserRoom(int user_id) {
    std::string room_id = ConnectionManager::instance().getUserRoom(user_id);
    if (room_id.empty()) return nullptr;
    
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    auto it = rooms_.find(room_id);
    if (it != rooms_.end()) {
        return it->second;
    }
    return nullptr;
}

/**
 * 广播消息到房间
 */
void GameServiceHandler::broadcastToRoom(const std::string& room_id, 
                                          const std::string& message, 
                                          int exclude_fd) {
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return;
    
    auto room = it->second;
    
    if (room->player1_fd >= 0 && room->player1_fd != exclude_fd) {
        sendToFd(room->player1_fd, message);
    }
    if (room->player2_fd >= 0 && room->player2_fd != exclude_fd) {
        sendToFd(room->player2_fd, message);
    }
}

/**
 * 发送消息给用户
 */
void GameServiceHandler::sendToUser(int user_id, const std::string& message) {
    ConnectionManager::instance().sendToUser(user_id, message);
}

/**
 * 发送消息到 fd
 */
void GameServiceHandler::sendToFd(int fd, const std::string& message) {
    ConnectionManager::instance().sendToFd(fd, message);
}
