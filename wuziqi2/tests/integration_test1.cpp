#include <iostream>
#include <string>
#include <cstring>
#include <curl/curl.h>
#include <jsoncpp/json/json.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>

#define AUTH_URL "http://127.0.0.1:8001"
#define MATCH_URL "http://127.0.0.1:8002"
#define GAME_WS_URL "ws://127.0.0.1:8003"

std::atomic<bool> g_running(true);
std::queue<std::string> g_messages;
std::mutex g_msg_mutex;

// ========== HTTP 工具 ==========
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string httpPost(const std::string& url, const std::string& body) {
    CURL* curl = curl_easy_init();
    std::string response;
    
    std::cout << "  [HTTP POST] 发送到: " << url << std::endl;
    std::cout << "  [HTTP POST] 请求体: " << body << std::endl;
    
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "HTTP请求失败: " << curl_easy_strerror(res) << std::endl;
        }
        
        std::cout << "  [HTTP POST] 响应: " << response << std::endl;
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return response;
}

std::string httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string response;
    
    std::cout << "  [HTTP GET] 发送到: " << url << std::endl;
    
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "HTTP请求失败: " << curl_easy_strerror(res) << std::endl;
        }
        
        std::cout << "  [HTTP GET] 响应: " << response << std::endl;
        
        curl_easy_cleanup(curl);
    }
    return response;
}

// ========== Base64 ==========
std::string base64_encode(const unsigned char* input, int length) {
    BIO* bio = BIO_new(BIO_f_base64());
    BIO* b64 = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, input, length);
    BIO_flush(bio);
    BUF_MEM* bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);
    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    return result;
}

// ========== WebSocket ==========
int wsConnect(const std::string& host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "  [WebSocket] 创建socket失败: " << strerror(errno) << std::endl;
        return -1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    std::cout << "  [WebSocket] 连接到 " << host << ":" << port << std::endl;
    
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "  [WebSocket] 连接失败: " << strerror(errno) << std::endl;
        close(fd);
        return -1;
    }
    
    std::cout << "  [WebSocket] 连接成功, fd=" << fd << std::endl;
    return fd;
}

bool wsHandshake(int fd, const std::string& path) {
    std::string host = "127.0.0.1:8003";
    std::string key = "dGhlIHNhbXBsZSBub25jZQ==";  // 固定key用于测试
    
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)key.c_str(), key.length(), hash);
    std::string accept = base64_encode(hash, SHA_DIGEST_LENGTH);
    
    std::string request = 
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    
    std::cout << "  [WebSocket] 发送握手请求: " << std::endl;
    std::cout << request << std::endl;
    
    if (send(fd, request.c_str(), request.length(), 0) < 0) {
        std::cerr << "  [WebSocket] 发送握手请求失败: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 设置超时
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    char buffer[1024];
    int n = recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        std::cerr << "  [WebSocket] 握手响应超时或失败, n=" << n << std::endl;
        return false;
    }
    buffer[n] = '\0';
    
    std::cout << "  [WebSocket] 握手响应: " << std::endl;
    std::cout << buffer << std::endl;
    
    bool ok = std::string(buffer).find("101") != std::string::npos;
    if (!ok) {
        std::cerr << "  [WebSocket] 握手失败, 未找到101状态码" << std::endl;
    }
    return ok;
}

std::string wsEncodeFrame(const std::string& message) {
    std::string frame = "\x81";  // FIN + text frame
    size_t len = message.length();
    if (len < 126) {
        frame += (char)len;
    } else {
        frame += (char)126;
        frame += (char)((len >> 8) & 0xFF);
        frame += (char)(len & 0xFF);
    }
    frame += message;
    return frame;
}

std::string wsDecodeFrame(const char* data, size_t len) {
    if (len < 2) return "";
    
    bool masked = (data[1] & 0x80) != 0;  // 检查掩码位
    int payload_len = data[1] & 0x7F;
    size_t offset = 2;
    
    // 处理扩展长度
    if (payload_len == 126) {
        if (len < 4) return "";
        payload_len = ((unsigned char)data[2] << 8) | (unsigned char)data[3];
        offset = 4;
    } else if (payload_len == 127) {
        if (len < 10) return "";
        // 跳过8字节长度
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | (unsigned char)data[2 + i];
        }
        offset = 10;
    }
    
    // 处理掩码
    unsigned char mask[4] = {0};
    if (masked) {
        if (len < offset + 4) return "";
        memcpy(mask, data + offset, 4);
        offset += 4;
    }
    
    if (len < offset + payload_len) return "";
    
    // 解码消息
    std::string message;
    message.reserve(payload_len);
    for (size_t i = 0; i < payload_len; i++) {
        char c = data[offset + i];
        if (masked) {
            c ^= mask[i % 4];
        }
        message += c;
    }
    
    return message;
}

bool wsSend(int fd, const std::string& message) {
    std::cout << "  [WebSocket] 发送消息: " << message << std::endl;
    std::string frame = wsEncodeFrame(message);
    int sent = send(fd, frame.c_str(), frame.length(), 0);
    bool ok = sent > 0;
    if (!ok) {
        std::cerr << "  [WebSocket] 发送失败: " << strerror(errno) << std::endl;
    }
    return ok;
}

std::string wsRecv(int fd) {
    char buffer[8192];
    
    // 设置超时
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;  // 500ms
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    int n = recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        if (n == 0) {
            std::cout << "  [WebSocket] 连接已关闭" << std::endl;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 超时，正常情况
        } else {
            std::cerr << "  [WebSocket] 接收失败: " << strerror(errno) << std::endl;
        }
        return "";
    }
    
    std::string frame = wsDecodeFrame(buffer, n);
    if (!frame.empty()) {
        std::cout << "  [WebSocket] 收到消息: " << frame << std::endl;
    } else {
        std::cout << "  [WebSocket] 收到空消息或解码失败, n=" << n << std::endl;
    }
    return frame;
}

// ========== 测试辅助 ==========
void printResult(const std::string& test, bool success, const std::string& msg = "") {
    std::cout << "[" << (success ? "✓" : "✗") << "] " << test;
    if (!msg.empty()) std::cout << ": " << msg;
    std::cout << std::endl;
}

Json::Value parseJson(const std::string& str) {
    Json::Value json;
    Json::Reader reader;
    reader.parse(str, json);
    return json;
}

bool registerUser(const std::string& username, const std::string& password) {
    Json::Value req;
    req["username"] = username;
    req["password"] = password;
    
    std::cout << "\n[注册用户] username=" << username << std::endl;
    std::string resp = httpPost(std::string(AUTH_URL) + "/api/register", Json::writeString(Json::StreamWriterBuilder(), req));
    
    if (resp.empty()) {
        std::cerr << "  [错误] 注册响应为空" << std::endl;
        return false;
    }
    
    Json::Value json = parseJson(resp);
    bool success = json["success"].asBool();
    if (!success) {
        std::cout << "  [注册失败] 响应: " << resp << std::endl;
    }
    return success;
}

std::string loginUser(const std::string& username, const std::string& password) {
    Json::Value req;
    req["username"] = username;
    req["password"] = password;
    
    std::cout << "\n[用户登录] username=" << username << std::endl;
    std::string resp = httpPost(std::string(AUTH_URL) + "/api/login", Json::writeString(Json::StreamWriterBuilder(), req));
    
    if (resp.empty()) {
        std::cerr << "  [错误] 登录响应为空" << std::endl;
        return "";
    }
    
    Json::Value json = parseJson(resp);
    if (json["success"].asBool()) {
        std::string token = json["data"]["token"].asString();
        std::cout << "  [登录成功] token=" << token << std::endl;
        return token;
    } else {
        std::cout << "  [登录失败] 响应: " << resp << std::endl;
        return "";
    }
}

bool verifyToken(const std::string& token) {
    Json::Value req;
    req["token"] = token;
    
    std::cout << "\n[验证Token] token长度=" << token.length() << std::endl;
    std::string resp = httpPost(std::string(AUTH_URL) + "/api/verify", Json::writeString(Json::StreamWriterBuilder(), req));
    
    if (resp.empty()) {
        std::cerr << "  [错误] 验证响应为空" << std::endl;
        return false;
    }
    
    Json::Value json = parseJson(resp);
    bool success = json["success"].asBool();
    std::cout << "  [验证结果] " << (success ? "成功" : "失败") << std::endl;
    if (!success) {
        std::cout << "  [验证失败详情] " << resp << std::endl;
    }
    return success;
}

int getUserIdFromToken(const std::string& token) {
    Json::Value req;
    req["token"] = token;
    
    std::cout << "\n[获取用户ID] token长度=" << token.length() << std::endl;
    std::string resp = httpPost(std::string(AUTH_URL) + "/api/verify", Json::writeString(Json::StreamWriterBuilder(), req));
    
    if (resp.empty()) {
        std::cerr << "  [错误] 获取用户ID响应为空" << std::endl;
        return -1;
    }
    
    Json::Value json = parseJson(resp);
    if (json["success"].asBool()) {
        int user_id = json["data"]["user_id"].asInt();
        std::cout << "  [获取成功] user_id=" << user_id << std::endl;
        return user_id;
    } else {
        std::cout << "  [获取失败] 响应: " << resp << std::endl;
        return -1;
    }
}

// ========== 匹配服务 ==========
std::string randomMatch(int user_id) {
    Json::Value req;
    req["user_id"] = user_id;
    
    std::cout << "\n[随机匹配] user_id=" << user_id << std::endl;
    std::string resp = httpPost(std::string(MATCH_URL) + "/api/random_match", Json::writeString(Json::StreamWriterBuilder(), req));
    
    if (resp.empty()) {
        std::cerr << "  [错误] 随机匹配响应为空" << std::endl;
    }
    
    return resp;
}

std::string cancelMatch(int user_id) {
    Json::Value req;
    req["user_id"] = user_id;
    
    std::cout << "\n[取消匹配] user_id=" << user_id << std::endl;
    std::string resp = httpPost(std::string(MATCH_URL) + "/api/cancel_match", Json::writeString(Json::StreamWriterBuilder(), req));
    
    if (resp.empty()) {
        std::cerr << "  [错误] 取消匹配响应为空" << std::endl;
    }
    
    return resp;
}

std::string createRoom(int user_id) {
    Json::Value req;
    req["user_id"] = user_id;
    
    std::cout << "\n[创建房间] user_id=" << user_id << std::endl;
    std::string resp = httpPost(std::string(MATCH_URL) + "/api/create_room", Json::writeString(Json::StreamWriterBuilder(), req));
    
    if (resp.empty()) {
        std::cerr << "  [错误] 创建房间响应为空" << std::endl;
    }
    
    return resp;
}

std::string joinRoom(int user_id, const std::string& invite_code) {
    Json::Value req;
    req["user_id"] = user_id;
    req["invite_code"] = invite_code;
    
    std::cout << "\n[加入房间] user_id=" << user_id << ", invite_code=" << invite_code << std::endl;
    std::string resp = httpPost(std::string(MATCH_URL) + "/api/join_room", Json::writeString(Json::StreamWriterBuilder(), req));
    
    if (resp.empty()) {
        std::cerr << "  [错误] 加入房间响应为空" << std::endl;
    }
    
    return resp;
}

std::string getMatchStatus(int user_id) {
    Json::Value req;
    req["user_id"] = user_id;
    
    std::cout << "\n[获取匹配状态] user_id=" << user_id << std::endl;
    std::string resp = httpPost(std::string(MATCH_URL) + "/api/match_status", Json::writeString(Json::StreamWriterBuilder(), req));
    
    if (resp.empty()) {
        std::cerr << "  [错误] 获取匹配状态响应为空" << std::endl;
    }
    
    return resp;
}

// ========== WebSocket 游戏 ==========
bool wsLogin(int fd, const std::string& token) {
    Json::Value msg;
    msg["type"] = "login";
    msg["data"]["token"] = token;
    
    std::cout << "\n[WebSocket登录] fd=" << fd << ", token长度=" << token.length() << std::endl;
    std::string req_str = Json::writeString(Json::StreamWriterBuilder(), msg);
    std::cout << "  [WebSocket登录请求] " << req_str << std::endl;
    
    wsSend(fd, req_str);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    std::string resp = wsRecv(fd);
    if (resp.empty()) {
        std::cerr << "  [错误] WebSocket登录响应为空" << std::endl;
        return false;
    }
    
    Json::Value json = parseJson(resp);
    bool ok = json["type"].asString() == "login_success" && json["success"].asBool();
    
    std::cout << "  [WebSocket登录结果] " << (ok ? "成功" : "失败") << std::endl;
    if (!ok) {
        std::cout << "  [WebSocket登录失败响应] " << resp << std::endl;
    }
    
    return ok;
}

bool wsSendMove(int fd, const std::string& room_id, int x, int y) {
    Json::Value msg;
    msg["type"] = "move";
    msg["data"]["room_id"] = room_id;
    msg["data"]["x"] = x;
    msg["data"]["y"] = y;
    
    std::cout << "\n[WebSocket发送落子] fd=" << fd << ", room_id=" << room_id 
              << ", pos=(" << x << "," << y << ")" << std::endl;
    
    wsSend(fd, Json::writeString(Json::StreamWriterBuilder(), msg));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    std::string resp = wsRecv(fd);
    if (resp.empty()) {
        std::cerr << "  [错误] WebSocket落子响应为空" << std::endl;
        return false;
    }
    
    Json::Value json = parseJson(resp);
    bool ok = json["type"].asString() == "move_received" && json["success"].asBool();
    
    std::cout << "  [WebSocket落子结果] " << (ok ? "成功" : "失败") << std::endl;
    if (!ok) {
        std::cout << "  [WebSocket落子失败响应] " << resp << std::endl;
    }
    
    return ok;
}

std::string wsWaitForMessage(int fd, const std::string& expectedType, int timeout_ms = 3000) {
    std::cout << "\n[等待WebSocket消息] fd=" << fd << ", 期望类型=" << expectedType 
              << ", 超时=" << timeout_ms << "ms" << std::endl;
    
    auto start = std::chrono::steady_clock::now();
    int attempt = 0;
    
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count() < timeout_ms) {
        
        attempt++;
        std::string resp = wsRecv(fd);
        
        if (!resp.empty()) {
            Json::Value json = parseJson(resp);
            std::string type = json["type"].asString();
            std::cout << "  [第" << attempt << "次尝试] 收到类型: " << type << std::endl;
            
            if (expectedType.empty() || type == expectedType) {
                std::cout << "  [匹配成功] 找到期望消息: " << resp << std::endl;
                return resp;
            } else {
                std::cout << "  [类型不匹配] 继续等待..." << std::endl;
            }
        } else {
            std::cout << "  [第" << attempt << "次尝试] 收到空消息" << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    std::cout << "  [超时] 在" << timeout_ms << "ms内未收到期望消息" << std::endl;
    return "";
}

// ========== 测试用例 ==========
bool testHealthCheck() {
    std::cout << "\n=== 测试 1: 健康检查 ===" << std::endl;
    
    std::cout << "\n[检查Auth服务健康]" << std::endl;
    std::string auth = httpGet(std::string(AUTH_URL) + "/health");
    bool auth_ok = auth.find("OK") != std::string::npos;
    printResult("Auth服务健康检查", auth_ok, auth_ok ? "OK" : auth);
    
    std::cout << "\n[检查Match服务健康]" << std::endl;
    std::string match = httpGet(std::string(MATCH_URL) + "/health");
    bool match_ok = match.find("OK") != std::string::npos;
    printResult("Match服务健康检查", match_ok, match_ok ? "OK" : match);
    
    std::cout << "\n[检查Game服务健康]" << std::endl;
    std::string game = httpGet("http://127.0.0.1:8004/health");
    bool game_ok = game.find("OK") != std::string::npos;
    printResult("Game服务健康检查", game_ok, game_ok ? "OK" : game);
    
    bool ok = auth_ok && match_ok && game_ok;
    return ok;
}

bool testAuthService() {
    std::cout << "\n=== 测试 2: 认证服务 ===" << std::endl;
    
    // 清理旧用户
    std::cout << "\n[清理旧用户]" << std::endl;
    std::string cleanup1 = httpPost(std::string(AUTH_URL) + "/api/login", 
        "{\"username\":\"test_user1\",\"password\":\"test123\"}");
    std::string cleanup2 = httpPost(std::string(AUTH_URL) + "/api/login", 
        "{\"username\":\"test_user2\",\"password\":\"test123\"}");
    
    // 注册用户1
    bool reg1 = registerUser("test_user1", "test123");
    printResult("注册用户1", reg1);
    
    // 注册用户2
    bool reg2 = registerUser("test_user2", "test123");
    printResult("注册用户2", reg2);
    
    // 登录用户1
    std::string token1 = loginUser("test_user1", "test123");
    bool login1 = !token1.empty();
    printResult("用户1登录", login1);
    
    // 登录用户2
    std::string token2 = loginUser("test_user2", "test123");
    bool login2 = !token2.empty();
    printResult("用户2登录", login2);
    
    // 验证token
    bool verify1 = verifyToken(token1);
    bool verify2 = verifyToken(token2);
    printResult("验证用户1 Token", verify1);
    printResult("验证用户2 Token", verify2);
    
    // 获取user_id
    int user1_id = getUserIdFromToken(token1);
    int user2_id = getUserIdFromToken(token2);
    printResult("获取用户1 ID", user1_id > 0, "user_id=" + std::to_string(user1_id));
    printResult("获取用户2 ID", user2_id > 0, "user_id=" + std::to_string(user2_id));
    
    bool ok = login1 && login2 && verify1 && verify2 && user1_id > 0 && user2_id > 0;
    
    if (ok) {
        std::cout << "\n[保存测试数据]" << std::endl;
        std::cout << "USER1_TOKEN=" << token1 << std::endl;
        std::cout << "USER2_TOKEN=" << token2 << std::endl;
        std::cout << "USER1_ID=" << user1_id << std::endl;
        std::cout << "USER2_ID=" << user2_id << std::endl;
    }
    
    return ok;
}

bool testRandomMatch(int user1_id, int user2_id) {
    std::cout << "\n=== 测试 3: 随机匹配 ===" << std::endl;
    
    // 取消之前的匹配状态
    std::cout << "\n[取消之前匹配状态]" << std::endl;
    std::string cancel1 = cancelMatch(user1_id);
    Json::Value cancel1_json = parseJson(cancel1);
    std::cout << "  用户1取消结果: " << cancel1_json.toStyledString() << std::endl;
    
    std::string cancel2 = cancelMatch(user2_id);
    Json::Value cancel2_json = parseJson(cancel2);
    std::cout << "  用户2取消结果: " << cancel2_json.toStyledString() << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 用户1发起随机匹配
    std::cout << "\n[用户1发起随机匹配]" << std::endl;
    std::string match1_resp = randomMatch(user1_id);
    Json::Value match1_json = parseJson(match1_resp);
    bool match1_ok = match1_json["success"].asBool();
    printResult("用户1发起随机匹配", match1_ok, match1_json["message"].asString());
    
    // 用户2发起随机匹配
    std::cout << "\n[用户2发起随机匹配]" << std::endl;
    std::string match2_resp = randomMatch(user2_id);
    Json::Value match2_json = parseJson(match2_resp);
    bool match2_ok = match2_json["success"].asBool();
    printResult("用户2发起随机匹配", match2_ok, match2_json["message"].asString());
    
    // 等待匹配完成
    std::cout << "\n[等待匹配完成]" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 检查匹配状态
    std::cout << "\n[检查用户1匹配状态]" << std::endl;
    std::string status1 = getMatchStatus(user1_id);
    Json::Value status1_json = parseJson(status1);
    bool in_room1 = status1_json["success"].asBool() && status1_json["data"]["in_room"].asBool();
    std::string room_id1 = in_room1 ? status1_json["data"]["room_id"].asString() : "";
    printResult("用户1匹配状态", status1_json["success"].asBool());
    printResult("用户1已进入房间", in_room1, "room_id=" + room_id1);
    
    std::cout << "\n[完整响应] " << status1_json.toStyledString() << std::endl;
    
    std::cout << "\n[检查用户2匹配状态]" << std::endl;
    std::string status2 = getMatchStatus(user2_id);
    Json::Value status2_json = parseJson(status2);
    bool in_room2 = status2_json["success"].asBool() && status2_json["data"]["in_room"].asBool();
    std::string room_id2 = in_room2 ? status2_json["data"]["room_id"].asString() : "";
    printResult("用户2匹配状态", status2_json["success"].asBool());
    printResult("用户2已进入房间", in_room2, "room_id=" + room_id2);
    
    return match1_ok && match2_ok && in_room1 && in_room2 && room_id1 == room_id2 && !room_id1.empty();
}

bool testRoomMatch(int user1_id, int user2_id) {
    std::cout << "\n=== 测试 4: 口令房间匹配 ===" << std::endl;
    
    // 清理之前的匹配状态
    std::cout << "\n[清理之前的匹配状态]" << std::endl;
    cancelMatch(user1_id);
    cancelMatch(user2_id);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 用户1创建房间
    std::string create_resp = createRoom(user1_id);
    Json::Value create_json = parseJson(create_resp);
    bool create_ok = create_json["success"].asBool();
    std::string room_id = create_ok ? create_json["data"]["room_id"].asString() : "";
    std::string invite_code = create_ok ? create_json["data"]["invite_code"].asString() : "";
    printResult("用户1创建房间", create_ok);
    
    if (!create_ok) {
        std::cout << "  [创建房间失败] 响应: " << create_resp << std::endl;
    } else {
        printResult("获取邀请码", !invite_code.empty(), "code=" + invite_code);
    }
    
    // 用户2通过口令加入
    std::string join_resp = joinRoom(user2_id, invite_code);
    Json::Value join_json = parseJson(join_resp);
    bool join_ok = join_json["success"].asBool();
    std::string join_room_id = join_ok ? join_json["data"]["room_id"].asString() : "";
    printResult("用户2口令加入房间", join_ok);
    
    if (!join_ok) {
        std::cout << "  [加入房间失败] 响应: " << join_resp << std::endl;
    } else {
        printResult("房间ID匹配", room_id == join_room_id, 
                   "user1_room=" + room_id + ", user2_room=" + join_room_id);
    }
    
    return create_ok && join_ok && room_id == join_room_id;
}

bool testWebSocketGame(int user1_id, const std::string& token1, int user2_id, const std::string& token2) {
    std::cout << "\n=== 测试 5: WebSocket 游戏测试 ===" << std::endl;
    
    // 清理之前的匹配状态
    std::cout << "\n[清理之前的匹配状态]" << std::endl;
    cancelMatch(user1_id);
    cancelMatch(user2_id);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 先连接 WebSocket 并登录
    std::cout << "\n[连接WebSocket]" << std::endl;
    int fd1 = wsConnect(GAME_WS_URL, 8003);
    int fd2 = wsConnect(GAME_WS_URL, 8003);
    
    bool ws_ok = fd1 > 0 && fd2 > 0;
    printResult("WebSocket连接", ws_ok);
    
    if (!ws_ok) {
        if (fd1 > 0) close(fd1);
        if (fd2 > 0) close(fd2);
        return false;
    }
    
    // WebSocket握手
    std::cout << "\n[WebSocket握手]" << std::endl;
    bool handshake1 = wsHandshake(fd1, "/ws/game");
    bool handshake2 = wsHandshake(fd2, "/ws/game");
    printResult("WebSocket握手", handshake1 && handshake2);
    
    if (!handshake1 || !handshake2) {
        close(fd1);
        close(fd2);
        return false;
    }
    
    // 先登录
    std::cout << "\n[WebSocket登录]" << std::endl;
    bool login_ws1 = wsLogin(fd1, token1);
    bool login_ws2 = wsLogin(fd2, token2);
    printResult("WebSocket用户1登录", login_ws1);
    printResult("WebSocket用户2登录", login_ws2);
    
    // 检查 game-service 连接状态
    std::cout << "\n[检查game-service状态]" << std::endl;
    std::string game_debug_url = "http://127.0.0.1:8004/internal/debug";
    std::string debug = httpGet(game_debug_url);
    std::cout << "  [game-service连接状态] " << debug << std::endl;
    
    // 等待用户连接稳定
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 然后创建房间让用户加入
    std::cout << "\n[创建测试房间]" << std::endl;
    std::string create_resp = createRoom(user1_id);
    Json::Value create_json = parseJson(create_resp);
    bool create_ok = create_json["success"].asBool();
    std::string room_id = create_ok ? create_json["data"]["room_id"].asString() : "";
    std::string invite_code = create_ok ? create_json["data"]["invite_code"].asString() : "";
    printResult("创建房间", create_ok, "room_id=" + room_id);
    
    if (!create_ok || room_id.empty()) {
        std::cerr << "  [错误] 创建房间失败: " << create_resp << std::endl;
        close(fd1);
        close(fd2);
        return false;
    }
    
    std::cout << "\n[用户2加入房间]" << std::endl;
    std::string join_resp = joinRoom(user2_id, invite_code);
    Json::Value join_json = parseJson(join_resp);
    bool join_ok = join_json["success"].asBool();
    std::string join_room_id = join_ok ? join_json["data"]["room_id"].asString() : "";
    printResult("加入房间", join_ok, "room_id=" + join_room_id);
    
    // 等待 room_ready 通知
    std::cout << "\n[等待房间就绪通知]" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 读取所有收到的消息
    std::string room_ready1 = wsWaitForMessage(fd1, "room_ready", 2000);
    std::string room_ready2 = wsWaitForMessage(fd2, "room_ready", 2000);
    printResult("用户1收到房间就绪", !room_ready1.empty());
    printResult("用户2收到房间就绪", !room_ready2.empty());
    
    if (room_ready1.empty()) {
        std::cerr << "  [警告] 用户1未收到room_ready" << std::endl;
    }
    if (room_ready2.empty()) {
        std::cerr << "  [警告] 用户2未收到room_ready" << std::endl;
    }
    
    // 用户1落子
    std::cout << "\n[用户1落子]" << std::endl;
    bool move1 = wsSendMove(fd1, room_id, 7, 7);
    printResult("用户1落子(7,7)", move1);
    
    // 检查用户2收到的消息
    std::cout << "\n[检查用户2是否收到落子]" << std::endl;
    std::string opp_move = wsWaitForMessage(fd2, "opponent_move", 2000);
    bool opp_move_ok = !opp_move.empty();
    printResult("用户2收到对手落子", opp_move_ok);
    
    if (opp_move_ok) {
        Json::Value opp_json = parseJson(opp_move);
        int x = opp_json["data"]["x"].asInt();
        int y = opp_json["data"]["y"].asInt();
        printResult("落子位置", x == 7 && y == 7, 
                   "期望(7,7), 实际(" + std::to_string(x) + "," + std::to_string(y) + ")");
    } else {
        std::cout << "  [调试] 用户2收到的所有消息: " << std::endl;
        // 尝试读取所有剩余消息
        for (int i = 0; i < 5; i++) {
            std::string any_msg = wsWaitForMessage(fd2, "", 200);
            if (!any_msg.empty()) {
                std::cout << "  - " << any_msg << std::endl;
            }
        }
    }
    
    // 用户2落子
    std::cout << "\n[用户2落子]" << std::endl;
    bool move2 = wsSendMove(fd2, room_id, 8, 8);
    printResult("用户2落子(8,8)", move2);
    
    // 用户1收到落子
    std::cout << "\n[检查用户1是否收到落子]" << std::endl;
    std::string opp_move2 = wsWaitForMessage(fd1, "opponent_move", 2000);
    bool opp_move2_ok = !opp_move2.empty();
    printResult("用户1收到对手落子", opp_move2_ok);
    
    if (opp_move2_ok) {
        Json::Value opp_json = parseJson(opp_move2);
        int x = opp_json["data"]["x"].asInt();
        int y = opp_json["data"]["y"].asInt();
        printResult("落子位置", x == 8 && y == 8, 
                   "期望(8,8), 实际(" + std::to_string(x) + "," + std::to_string(y) + ")");
    } else {
        std::cout << "  [调试] 用户1收到的所有消息: " << std::endl;
        // 尝试读取所有剩余消息
        for (int i = 0; i < 5; i++) {
            std::string any_msg = wsWaitForMessage(fd1, "", 200);
            if (!any_msg.empty()) {
                std::cout << "  - " << any_msg << std::endl;
            }
        }
    }
    
    // 清理
    close(fd1);
    close(fd2);
    
    std::cout << "\n[WebSocket测试总结]" << std::endl;
    std::cout << "  - WebSocket登录: " << (login_ws1 && login_ws2 ? "成功" : "失败") << std::endl;
    std::cout << "  - 房间就绪: " << (!room_ready1.empty() && !room_ready2.empty() ? "成功" : "失败") << std::endl;
    std::cout << "  - 落子交互: " << (move1 && move2 && opp_move_ok && opp_move2_ok ? "成功" : "失败") << std::endl;
    
    return login_ws1 && login_ws2 && move1 && move2;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "    五子棋游戏集成测试" << std::endl;
    std::cout << "========================================" << std::endl;
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    int passed = 0;
    int total = 5;
    
    // 测试1: 健康检查
    std::cout << "\n\n>>> 开始测试1: 健康检查" << std::endl;
    if (testHealthCheck()) {
        passed++;
        std::cout << "\n[测试1通过]" << std::endl;
    } else {
        std::cerr << "\n[错误] 服务未就绪，请检查各服务是否启动" << std::endl;
    }
    
    // 测试2: 认证服务
    std::cout << "\n\n>>> 开始测试2: 认证服务" << std::endl;
    if (testAuthService()) {
        passed++;
        std::cout << "\n[测试2通过]" << std::endl;
    } else {
        std::cerr << "\n[错误] 认证服务测试失败" << std::endl;
    }
    
    // 从环境变量或使用测试用户
    std::string token1, token2;
    int user1_id = -1, user2_id = -1;
    
    // 尝试从环境变量获取
    char* env_token1 = getenv("USER1_TOKEN");
    char* env_token2 = getenv("USER2_TOKEN");
    char* env_user1 = getenv("USER1_ID");
    char* env_user2 = getenv("USER2_ID");
    
    if (env_token1 && env_token2) {
        token1 = env_token1;
        token2 = env_token2;
        user1_id = env_user1 ? std::stoi(env_user1) : getUserIdFromToken(token1);
        user2_id = env_user2 ? std::stoi(env_user2) : getUserIdFromToken(token2);
    } else {
        // 登录获取token
        token1 = loginUser("test_user1", "test123");
        token2 = loginUser("test_user2", "test123");
        user1_id = getUserIdFromToken(token1);
        user2_id = getUserIdFromToken(token2);
    }
    
    if (token1.empty() || token2.empty() || user1_id < 0 || user2_id < 0) {
        std::cerr << "\n[错误] 无法获取有效的token和user_id" << std::endl;
        std::cerr << "token1: " << token1 << std::endl;
        std::cerr << "token2: " << token2 << std::endl;
        std::cerr << "user1_id: " << user1_id << std::endl;
        std::cerr << "user2_id: " << user2_id << std::endl;
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "    测试完成 (" << passed << "/" << total << " 通过)" << std::endl;
        std::cout << "========================================" << std::endl;
        
        curl_global_cleanup();
        return 1;
    }
    
    std::cout << "\n[测试用户]" << std::endl;
    std::cout << "用户1: id=" << user1_id << std::endl;
    std::cout << "用户2: id=" << user2_id << std::endl;
    
    // 测试3: 随机匹配
    std::cout << "\n\n>>> 开始测试3: 随机匹配" << std::endl;
    if (testRandomMatch(user1_id, user2_id)) {
        passed++;
        std::cout << "\n[测试3通过]" << std::endl;
    } else {
        std::cerr << "\n[警告] 随机匹配测试失败" << std::endl;
    }
    
    // 测试4: 口令房间匹配
    std::cout << "\n\n>>> 开始测试4: 口令房间匹配" << std::endl;
    if (testRoomMatch(user1_id, user2_id)) {
        passed++;
        std::cout << "\n[测试4通过]" << std::endl;
    } else {
        std::cerr << "\n[警告] 口令房间匹配测试失败" << std::endl;
    }
    
    // 测试5: WebSocket游戏
    std::cout << "\n\n>>> 开始测试5: WebSocket游戏" << std::endl;
    if (testWebSocketGame(user1_id, token1, user2_id, token2)) {
        passed++;
        std::cout << "\n[测试5通过]" << std::endl;
    } else {
        std::cerr << "\n[警告] WebSocket游戏测试失败" << std::endl;
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "    测试完成 (" << passed << "/" << total << " 通过)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    if (passed == total) {
        std::cout << "\n🎉 所有测试通过！" << std::endl;
    } else {
        std::cout << "\n⚠️  部分测试失败，请检查日志" << std::endl;
    }
    
    curl_global_cleanup();
    return passed == total ? 0 : 1;
}