/**
 * @file handler.cpp
 * @brief 匹配服务处理器实现
 */

#include "handler.h"

// =====================================================
// 第一部分：构造和初始化
// =====================================================

/**
 * @brief 构造函数
 */
MatchServiceHandler::MatchServiceHandler(std::shared_ptr<RedisPool> redis,
                                       std::shared_ptr<ThreadPool> pool,
                                       const std::string& game_service_url)
    : redis_(redis)
    , pool_(pool)
    , game_service_url_(game_service_url) {
    
    // 创建 HTTP 客户端
    http_ = std::make_shared<HttpClient>();
    http_->setTimeout(5000);  // 5秒超时
}

/**
 * @brief 初始化
 */
bool MatchServiceHandler::init() {
    LOG_INFO("match-service 初始化开始");
    // 匹配服务不需要初始化数据库
    // Redis 连接池已经初始化
    LOG_INFO("match-service 初始化完成");
    return true;
}

// =====================================================
// 第二部分：加入匹配
// =====================================================

/**
 * @brief 处理加入匹配请求
 */
std::string MatchServiceHandler::handleJoinMatch(int user_id, 
                                                 const std::string& username, 
                                                 int level) {
    Json::Value response;
    response["type"] = "join_match";

    // 验证参数
    if (user_id <= 0) {
        response["success"] = false;
        response["message"] = "无效的用户 ID";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    if (username.empty()) {
        response["success"] = false;
        response["message"] = "用户名不能为空";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    if (level < 1 || level > 100) {
        response["success"] = false;
        response["message"] = "等级必须在 1-100 之间";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // Step 1: 计算桶编号
    int bucket = calculateBucket(level);
    LOG_DEBUG("用户 %d (等级 %d) 加入桶 %d", user_id, level, bucket);

    // Step 2: 检查是否已在队列中
    std::string wait_key = "match:wait:" + std::to_string(user_id);
    if (redis_->exists(wait_key)) {
        response["success"] = false;
        response["message"] = "已经在匹配队列中";
        response["data"]["bucket"] = bucket;
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // Step 3: 创建玩家信息
    PlayerInfo player;
    player.user_id = user_id;
    player.username = username;
    player.level = level;
    player.timestamp = Utils::getCurrentTimestamp();
    player.wait_seconds = 0;

    // 序列化为 JSON
    Json::Value player_json;
    player_json["user_id"] = player.user_id;
    player_json["username"] = player.username;
    player_json["level"] = player.level;
    player_json["timestamp"] = player.timestamp;
    
    std::string player_str = Json::writeString(Json::StreamWriterBuilder(), player_json);

    // Step 4: 加入匹配队列
    std::string queue_key = "match:queue:" + std::to_string(bucket);
    redis_->rpush(queue_key, player_str);

    // Step 5: 记录等待状态
    redis_->set(wait_key, std::to_string(player.timestamp), match_timeout_);

    // Step 6: 尝试匹配
    auto match_result = tryMatch(bucket);
    
    if (match_result.first.user_id != 0 && match_result.second.user_id != 0) {
        // 匹配成功！
        std::string room_id = notifyGameService(match_result.first, match_result.second);
        
        // 保存匹配结果
        std::string result_key1 = "match:result:" + std::to_string(match_result.first.user_id);
        std::string result_key2 = "match:result:" + std::to_string(match_result.second.user_id);
        
        Json::Value result_json;
        result_json["room_id"] = room_id;
        result_json["opponent_id"] = match_result.second.user_id;
        result_json["opponent_name"] = match_result.second.username;
        
        std::string result_str = Json::writeString(Json::StreamWriterBuilder(), result_json);
        
        redis_->set(result_key1, result_str, 60);
        redis_->set(result_key2, result_str, 60);

        response["success"] = true;
        response["message"] = "匹配成功";
        response["data"]["matched"] = true;
        response["data"]["room_id"] = room_id;
        
        LOG_INFO("匹配成功: room=%s, player1=%s, player2=%s", 
                 room_id.c_str(),
                 match_result.first.username.c_str(),
                 match_result.second.username.c_str());
    } else {
        // 还在等待中
        response["success"] = true;
        response["message"] = "已加入匹配队列";
        response["data"]["matched"] = false;
        response["data"]["bucket"] = bucket;
        response["data"]["wait_seconds"] = 0;
        
        LOG_INFO("用户 %s 加入匹配队列，等待中...", username.c_str());
    }

    return Json::writeString(Json::StreamWriterBuilder(), response);
}

// =====================================================
// 第三部分：离开匹配
// =====================================================

/**
 * @brief 处理离开匹配请求
 */
std::string MatchServiceHandler::handleLeaveMatch(int user_id) {
    Json::Value response;
    response["type"] = "leave_match";

    if (user_id <= 0) {
        response["success"] = false;
        response["message"] = "无效的用户 ID";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // 检查是否在队列中
    std::string wait_key = "match:wait:" + std::to_string(user_id);
    if (!redis_->exists(wait_key)) {
        response["success"] = false;
        response["message"] = "不在匹配队列中";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // 从所有桶中移除
    // 注意：这里简化了，实际需要知道在哪个桶
    for (int bucket = 1; bucket <= 5; bucket++) {
        std::string queue_key = "match:queue:" + std::to_string(bucket);
        // 遍历队列，删除该用户的记录
        auto members = redis_->lrange(queue_key, 0, -1);
        for (size_t i = 0; i < members.size(); i++) {
            Json::Value player_json;
            if (Json::Reader().parse(members[i], player_json)) {
                if (player_json["user_id"].asInt() == user_id) {
                    // 从队列中移除
                    redis_->lrem(queue_key, 1, members[i]);
                    break;
                }
            }
        }
    }

    // 删除等待状态
    redis_->del(wait_key);

    response["success"] = true;
    response["message"] = "已离开匹配队列";
    
    LOG_INFO("用户 %d 离开匹配队列", user_id);
    return Json::writeString(Json::StreamWriterBuilder(), response);
}

// =====================================================
// 第四部分：查询匹配状态
// =====================================================

/**
 * @brief 查询匹配状态
 */
std::string MatchServiceHandler::handleGetMatchStatus(int user_id) {
    Json::Value response;
    response["type"] = "match_status";

    if (user_id <= 0) {
        response["success"] = false;
        response["message"] = "无效的用户 ID";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // 检查是否有匹配结果
    std::string result_key = "match:result:" + std::to_string(user_id);
    std::string result_str = redis_->get(result_key);
    
    if (!result_str.empty()) {
        // 已匹配成功
        Json::Value result_json;
        if (Json::Reader().parse(result_str, result_json)) {
            response["success"] = true;
            response["data"]["status"] = "matched";
            response["data"]["room_id"] = result_json["room_id"];
            response["data"]["opponent_id"] = result_json["opponent_id"];
            response["data"]["opponent_name"] = result_json["opponent_name"];
            
            // 删除结果（只返回一次）
            redis_->del(result_key);
            
            return Json::writeString(Json::StreamWriterBuilder(), response);
        }
    }

    // 检查是否在队列中
    std::string wait_key = "match:wait:" + std::to_string(user_id);
    std::string wait_time_str = redis_->get(wait_key);
    
    if (wait_time_str.empty()) {
        response["success"] = true;
        response["data"]["status"] = "not_in_queue";
        return Json::writeString(Json::StreamWriterBuilder(), response);
    }

    // 计算等待时间
    long long join_time = std::stoll(wait_time_str);
    long long now = Utils::getCurrentTimestamp();
    int wait_seconds = static_cast<int>(now - join_time);

    response["success"] = true;
    response["data"]["status"] = "waiting";
    response["data"]["wait_seconds"] = wait_seconds;

    return Json::writeString(Json::StreamWriterBuilder(), response);
}

// =====================================================
// 第五部分：获取匹配结果
// =====================================================

/**
 * @brief 获取匹配结果
 */
std::string MatchServiceHandler::handleGetMatchResult(int user_id) {
    // 这个接口和 handleGetMatchStatus 类似
    // 返回匹配结果或空
    return handleGetMatchStatus(user_id);
}

// =====================================================
// 第六部分：核心匹配逻辑
// =====================================================

/**
 * @brief 计算桶编号
 * 
 * 等级 1-20   → 桶 1
 * 等级 21-40  → 桶 2
 * 等级 41-60  → 桶 3
 * 等级 61-80  → 桶 4
 * 等级 81-100 → 桶 5
 */
int MatchServiceHandler::calculateBucket(int level) {
    return (level - 1) / bucket_size_ + 1;
}

/**
 * @brief 尝试匹配
 * 
 * 匹配算法：
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  1. 从队列头部取出一个玩家                              │
 *   │     LPOP match:queue:{bucket}                         │
 *   │                                                         │
 *   │  2. 再取出一个玩家                                     │
 *   │     LPOP match:queue:{bucket}                         │
 *   │                                                         │
 *   │  3. 两人都还在等待？                                   │
 *   │     - 是: 匹配成功                                     │
 *   │     - 否: 把取出的玩家放回去                            │
 *   │                                                         │
 *   │  4. 如果队列只有一个人，放回去等待                       │
 *   └─────────────────────────────────────────────────────────┘
 */
std::pair<PlayerInfo, PlayerInfo> MatchServiceHandler::tryMatch(int bucket) {
    std::pair<PlayerInfo, PlayerInfo> result;
    result.first.user_id = 0;
    result.second.user_id = 0;

    std::string queue_key = "match:queue:" + std::to_string(bucket);

    // 取第一个玩家
    std::string player1_str = redis_->lpop(queue_key);
    if (player1_str.empty()) {
        // 队列为空
        return result;
    }

    // 解析第一个玩家
    Json::Value player1_json;
    if (!Json::Reader().parse(player1_str, player1_json)) {
        LOG_ERROR("解析玩家信息失败: %s", player1_str.c_str());
        return result;
    }

    PlayerInfo player1;
    player1.user_id = player1_json["user_id"].asInt();
    player1.username = player1_json["username"].asString();
    player1.level = player1_json["level"].asInt();
    player1.timestamp = player1_json["timestamp"].asInt64();

    // 检查第一个玩家是否还在等待
    std::string wait_key1 = "match:wait:" + std::to_string(player1.user_id);
    if (!redis_->exists(wait_key1)) {
        // 玩家已经离开，跳过
        return result;
    }

    // 取第二个玩家
    std::string player2_str = redis_->lpop(queue_key);
    if (player2_str.empty()) {
        // 队列只剩一个人，放回去
        redis_->rpush(queue_key, player1_str);
        return result;
    }

    // 解析第二个玩家
    Json::Value player2_json;
    if (!Json::Reader().parse(player2_str, player2_json)) {
        LOG_ERROR("解析玩家信息失败: %s", player2_str.c_str());
        // 把第一个玩家放回去
        redis_->rpush(queue_key, player1_str);
        return result;
    }

    PlayerInfo player2;
    player2.user_id = player2_json["user_id"].asInt();
    player2.username = player2_json["username"].asString();
    player2.level = player2_json["level"].asInt();
    player2.timestamp = player2_json["timestamp"].asInt64();

    // 检查第二个玩家是否还在等待
    std::string wait_key2 = "match:wait:" + std::to_string(player2.user_id);
    if (!redis_->exists(wait_key2)) {
        // 玩家已经离开，第一个放回去，尝试第二个
        redis_->rpush(queue_key, player1_str);
        return result;
    }

    // 匹配成功！
    result.first = player1;
    result.second = player2;

    // 删除两人的等待状态
    redis_->del(wait_key1);
    redis_->del(wait_key2);

    LOG_INFO("桶 %d 匹配成功: %s vs %s", 
             bucket, player1.username.c_str(), player2.username.c_str());

    return result;
}

/**
 * @brief 通知游戏服务创建房间
 */
std::string MatchServiceHandler::notifyGameService(const PlayerInfo& player1, 
                                                  const PlayerInfo& player2) {
    // 生成房间 ID
    std::string room_id = "room_" + std::to_string(Utils::getCurrentTimestampMs()) + "_" +
                          std::to_string(player1.user_id) + "_" + std::to_string(player2.user_id);

    // 构建请求
    Json::Value req_json;
    req_json["room_id"] = room_id;
    req_json["players"][0]["user_id"] = player1.user_id;
    req_json["players"][0]["username"] = player1.username;
    req_json["players"][0]["level"] = player1.level;
    req_json["players"][1]["user_id"] = player2.user_id;
    req_json["players"][1]["username"] = player2.username;
    req_json["players"][1]["level"] = player2.level;
    
    std::string req_body = Json::writeString(Json::StreamWriterBuilder(), req_json);

    // 发送 HTTP 请求
    std::string url = game_service_url_ + "/api/room/create";
    auto resp = http_->post(url, req_body, {{"Content-Type", "application/json"}});

    if (!resp.success) {
        LOG_ERROR("通知游戏服务失败: %s", resp.error.c_str());
        // 即使通知失败，也返回 room_id
        // 游戏服务可能会定期同步数据
    } else {
        LOG_INFO("通知游戏服务成功: %s", resp.body.c_str());
    }

    return room_id;
}

/**
 * @brief 检查超时并扩大匹配范围
 */
int MatchServiceHandler::checkTimeoutAndExpand(int bucket, const PlayerInfo& player) {
    long long now = Utils::getCurrentTimestamp();
    int wait_seconds = static_cast<int>(now - player.timestamp);

    // 等待超过 30 秒，扩大一个桶
    if (wait_seconds >= wait_timeout_ && bucket > 1) {
        return bucket - 1;
    }

    // 等待超过 60 秒，扩大两个桶
    if (wait_seconds >= max_wait_time_ && bucket > 2) {
        return bucket - 2;
    }

    return -1;  // 不需要扩大
}

// =====================================================
// 使用示例（注释）
// =====================================================
/*
// 示例 1：玩家加入匹配
void example1(MatchServiceHandler& handler) {
    std::string result = handler.handleJoinMatch(1001, "alice", 45);
    
    Json::Value json;
    Json::Reader().parse(result, json);
    
    if (json["success"].asBool()) {
        if (json["data"]["matched"].asBool()) {
            // 立即匹配成功
            std::cout << "匹配成功！房间号: " << json["data"]["room_id"].asString() << std::endl;
        } else {
            // 等待中
            std::cout << "已加入队列，请等待..." << std::endl;
        }
    }
}

// 示例 2：玩家查询状态
void example2(MatchServiceHandler& handler) {
    while (true) {
        std::string result = handler.handleGetMatchStatus(1001);
        
        Json::Value json;
        Json::Reader().parse(result, json);
        
        std::string status = json["data"]["status"].asString();
        
        if (status == "matched") {
            std::cout << "匹配成功！房间号: " << json["data"]["room_id"].asString() << std::endl;
            break;
        } else if (status == "not_in_queue") {
            std::cout << "不在队列中" << std::endl;
            break;
        } else {
            int wait = json["data"]["wait_seconds"].asInt();
            std::cout << "等待中... " << wait << "秒" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

// 示例 3：玩家离开匹配
void example3(MatchServiceHandler& handler) {
    std::string result = handler.handleLeaveMatch(1001);
    
    Json::Value json;
    Json::Reader().parse(result, json);
    
    if (json["success"].asBool()) {
        std::cout << "已离开匹配队列" << std::endl;
    }
}
*/
