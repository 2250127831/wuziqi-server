/**
 * @file main.cpp
 * @brief 游戏服务入口
 * 
 * =====================================================
 * 游戏服务架构
 * =====================================================
 * 
 * 游戏服务同时提供两种接口：
 * 
 * 1. HTTP REST API
 *    - match-service 调用创建房间
 *    - 玩家查询房间状态
 * 
 * 2. WebSocket 实时通信
 *    - 玩家实时落子
 *    - 服务器实时推送（对手落子、游戏状态）
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │                  Main Server                            │
 *   ├─────────────────────────────────────────────────────────┤
 *   │                                                         │
 *   │  ┌─────────────────────────────────────────────────┐   │
 *   │  │  HTTP Server (port)                            │   │
 *   │  │  - POST /api/room/create                       │   │
 *   │  │  - POST /api/room/join                         │   │
 *   │  │  - GET /api/room/:id                           │   │
 *   │  └─────────────────────────────────────────────────┘   │
 *   │                                                         │
 *   │  ┌─────────────────────────────────────────────────┐   │
 *   │  │  WebSocket Server (/ws/game)                   │   │
 *   │  │  - 玩家认证后加入                              │   │
 *   │  │  - 处理游戏消息（落子、投降等）                 │   │
 *   │  └─────────────────────────────────────────────────┘   │
 *   │                                                         │
 *   └─────────────────────────────────────────────────────────┘
 * 
 * =====================================================
 * 启动流程
 * =====================================================
 * 
 *   1. 解析命令行参数（端口）
 *   2. 初始化日志系统
 *   3. 初始化 Redis 连接池
 *   4. 初始化游戏处理器
 *   5. 启动 HTTP + WebSocket 服务器
 *   6. 进入事件循环
 */

#include <iostream>        // std::cout, std::cerr
#include <string>          // std::string
#include <cstring>         // strcmp
#include <signal.h>         // signal, SIGINT, SIGTERM
#include <sys/socket.h>    // socket, bind, listen
#include <netinet/in.h>    // sockaddr_in, INADDR_ANY
#include <arpa/inet.h>     // inet_ntoa
#include <unistd.h>        // close, fork
#include <fcntl.h>         // fcntl, F_GETFL, F_SETFL
#include <errno.h>         // errno
#include <poll.h>          // poll, POLLIN, POLLHUP
#include <openssl/sha.h>   // SHA1
#include <netinet/tcp.h>   // TCP_NODELAY

#include "handler.h"       // 游戏处理器
#include "logger.h"        // 日志系统
#include "redis_pool.h"    // Redis 连接池
#include "utils.h"         // 工具函数

// =============================================================================
// 全局变量
// =============================================================================

/** @brief 服务器监听端口 */
int g_port = 8083;

/** @brief 服务器 socket */
int g_server_fd = -1;

/** @brief 运行标志 */
volatile bool g_running = true;

// =============================================================================
// 信号处理
// =============================================================================

/**
 * 信号处理函数
 * 
 * 当收到 SIGINT (Ctrl+C) 或 SIGTERM 时优雅退出
 */
void signal_handler(int sig) {
    LOG_INFO("收到信号 %d，准备退出...", sig);
    g_running = false;
    
    // 关闭监听 socket，触发 accept 返回
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
}

// =============================================================================
// WebSocket 相关
// =============================================================================

/**
 * 计算 WebSocket 握手响应
 * 
 * WebSocket 握手过程：
 * 
 *   客户端                             服务器
 *     │                                  │
 *     │  GET /ws/game HTTP/1.1           │
 *     │  Upgrade: websocket              │
 *     │  Connection: Upgrade             │
 *     │  Sec-WebSocket-Key: dGhl...     │
 *     │────────────────────────────────►│
 *     │                                  │
 *     │              HTTP/1.1 101 Switching Protocols │
 *     │              Upgrade: websocket │
 *     │              Sec-WebSocket-Accept: 资源...   │
 *     │◄─────────────────────────────────│
 *     │                                  │
 *     │         握手完成！               │
 *     │                                  │
 *     │  ─── WebSocket 帧 ───►          │
 *     │  ◄── WebSocket 帧 ───          │
 *     │                                  │
 * 
 * 握手 key 计算方法：
 *   accept = Base64(SHA1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
 */
std::string compute_accept_key(const std::string& key) {
    // 固定的 GUID
    static const std::string GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    
    // 拼接 key + GUID
    std::string input = key + GUID;
    
    // 计算 SHA1
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)input.c_str(), input.length(), hash);
    
    // Base64 编码
    return Utils::base64Encode(std::string((char*)hash, SHA_DIGEST_LENGTH));
}

/**
 * 解析 HTTP 请求
 * 
 * @return -1 解析失败，0 GET 请求，1 WebSocket 握手
 */
int parse_http_request(const char* buf, int len, 
                       std::string& method, std::string& path,
                       std::string& ws_key) {
    // 查找 HTTP 方法结束位置
    const char* method_end = strstr(buf, " ");
    if (!method_end) return -1;
    
    method.assign(buf, method_end - buf);
    
    // 查找路径结束位置
    const char* path_end = strstr(method_end + 1, " ");
    if (!path_end) return -1;
    
    path.assign(method_end + 1, path_end - method_end - 1);
    
    // 检查是否是 WebSocket 握手
    if (method == "GET" && path.find("/ws/") == 0) {
        // 提取 Sec-WebSocket-Key
        const char* key_start = strstr(buf, "Sec-WebSocket-Key: ");
        if (key_start) {
            key_start += 19;  // 跳过 "Sec-WebSocket-Key: "
            const char* key_end = strstr(key_start, "\r\n");
            if (key_end) {
                ws_key.assign(key_start, key_end - key_start);
                return 1;
            }
        }
    }
    
    return 0;
}

/**
 * 编码 WebSocket 帧
 * 
 * WebSocket 帧格式：
 * 
 *   ┌─────────┬───────┬─────────┬─────────┬─────────┐
 *   │ FIN(1)  │ RSV(3)│ OPCODE  │ MASK(1) │ LENGTH  │
 *   │ 标志    │ 保留  │ 0:text  │ 是否有掩码│ 数据长度 │
 *   ├─────────┴───────┴─────────┴─────────┴─────────┤
 *   │                 MASKING-KEY (如果有)           │
 *   ├───────────────────────────────────────────────┤
 *   │                    PAYLOAD                    │
 *   │                     数据                       │
 *   └───────────────────────────────────────────────┘
 */
std::string encode_ws_frame(const std::string& data) {
    std::string frame;
    
    // 第一个字节：FIN + opcode (text = 0x81)
    frame.push_back(0x81);
    
    // 第二个字节：mask + length
    if (data.length() < 126) {
        frame.push_back((char)data.length());
    } else if (data.length() < 65536) {
        frame.push_back(126);
        frame.push_back((data.length() >> 8) & 0xFF);
        frame.push_back(data.length() & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((data.length() >> (i * 8)) & 0xFF);
        }
    }
    
    // 添加数据
    frame += data;
    
    return frame;
}

/**
 * 解码 WebSocket 帧
 * 
 * @return 空字符串表示需要更多数据，特殊值 "CLOSE" 表示关闭帧
 */
std::string decode_ws_frame(const char* buf, int len, int& out_len) {
    if (len < 2) {
        out_len = 0;
        return "";
    }
    
    // 解析第一个字节
    bool fin = (buf[0] & 0x80) != 0;
    int opcode = buf[0] & 0x0F;
    
    // 解析第二个字节
    bool masked = (buf[1] & 0x80) != 0;
    int payload_len = buf[1] & 0x7F;
    
    int header_len = 2;
    
    // 扩展长度
    if (payload_len == 126) {
        if (len < 4) { out_len = 0; return ""; }
        payload_len = ((buf[2] << 8) | buf[3]);
        header_len = 4;
    } else if (payload_len == 127) {
        if (len < 10) { out_len = 0; return ""; }
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | ((unsigned char)buf[2 + i]);
        }
        header_len = 10;
    }
    
    // 检查数据是否完整
    if (len < header_len + payload_len) {
        out_len = 0;
        return "";
    }
    
    out_len = header_len + payload_len;
    
    // 处理 opcode
    if (opcode == 0x08) {
        // 关闭帧
        return "CLOSE";
    }
    
    if (opcode != 0x01) {
        // 非文本帧，忽略
        return "";
    }
    
    // 获取数据位置
    const char* payload = buf + header_len;
    
    // 如果有掩码，解掩
    if (masked) {
        const char* mask = buf + header_len - 4;
        std::string result;
        result.resize(payload_len);
        for (int i = 0; i < payload_len; i++) {
            result[i] = payload[i] ^ mask[i % 4];
        }
        return result;
    } else {
        return std::string(payload, payload_len);
    }
}

// =============================================================================
// HTTP 请求处理
// =============================================================================

/**
 * 发送 HTTP 响应
 */
void send_http_response(int fd, int status, const std::string& body,
                        const std::string& content_type = "application/json") {
    std::ostringstream oss;
    
    // 状态行
    if (status == 200) {
        oss << "HTTP/1.1 200 OK\r\n";
    } else if (status == 400) {
        oss << "HTTP/1.1 400 Bad Request\r\n";
    } else if (status == 404) {
        oss << "HTTP/1.1 404 Not Found\r\n";
    } else {
        oss << "HTTP/1.1 500 Internal Server Error\r\n";
    }
    
    // 头部
    oss << "Content-Type: " << content_type << "\r\n";
    oss << "Content-Length: " << body.length() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    
    // 发送
    std::string header = oss.str();
    send(fd, header.c_str(), header.length(), 0);
    send(fd, body.c_str(), body.length(), 0);
}

/**
 * 处理 HTTP 请求
 */
void handle_http_request(int fd, const std::string& method, 
                        const std::string& path,
                        const std::string& body) {
    static GameServiceHandler handler;
    static bool initialized = false;
    
    if (!initialized) {
        handler.init();
        initialized = true;
    }
    
    std::string response;
    
    // 路由处理
    if (path == "/api/room/create" && method == "POST") {
        response = handler.handleCreateRoom(body);
    }
    else if (path.find("/api/room/join/") == 0 && method == "POST") {
        std::string room_id = path.substr(14);  // 去掉 "/api/room/join/"
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(body, root)) {
            int user_id = root["user_id"].asInt();
            response = handler.handleJoinRoom(room_id, user_id);
        } else {
            response = "{\"success\":false,\"error\":\"Invalid JSON\"}";
        }
    }
    else if (path.find("/api/room/") == 0 && method == "GET") {
        std::string room_id = path.substr(10);  // 去掉 "/api/room/"
        response = handler.handleGetRoom(room_id);
    }
    else {
        response = "{\"error\":\"Not found\"}";
        send_http_response(fd, 404, response);
        return;
    }
    
    send_http_response(fd, 200, response);
}

// =============================================================================
// WebSocket 消息处理
// =============================================================================

/**
 * 处理 WebSocket 消息
 */
void handle_ws_message(int fd, int user_id, const std::string& message) {
    static GameServiceHandler handler;
    handler.handleWebSocketMessage(fd, user_id, message);
}

/**
 * 获取 HTTP 请求体
 */
std::string read_http_body(const char* buf, int len) {
    // 查找空行位置（\r\n\r\n）
    const char* header_end = strstr(buf, "\r\n\r\n");
    if (!header_end) return "";
    
    header_end += 4;  // 跳过 \r\n\r\n
    
    // 查找 Content-Length
    const char* cl = strstr(buf, "Content-Length: ");
    if (!cl) return "";
    
    cl += 16;  // 跳过 "Content-Length: "
    const char* cl_end = strstr(cl, "\r\n");
    if (!cl_end) return "";
    
    int content_length = atoi(std::string(cl, cl_end - cl).c_str());
    
    // 返回 body 部分
    int body_len = len - (header_end - buf);
    if (body_len > content_length) {
        body_len = content_length;
    }
    
    return std::string(header_end, body_len);
}

// =============================================================================
// 主函数
// =============================================================================

/**
 * 打印使用说明
 */
void print_usage(const char* prog) {
    std::cout << "用法: " << prog << " [选项]\n";
    std::cout << "选项:\n";
    std::cout << "  -p <端口>    指定监听端口 (默认: 8083)\n";
    std::cout << "  -h           显示帮助\n";
}

/**
 * 主函数
 */
int main(int argc, char* argv[]) {
    // ==================== 参数解析 ====================
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            g_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    // ==================== 初始化 ====================
    
    // 初始化日志
    Logger::instance().init("game-service.log");
    LOG_INFO("游戏服务启动，端口: %d", g_port);
    
    // 初始化 Redis 连接池
    redis_pool_config config;
    config.host = "127.0.0.1";
    config.port = 6379;
    config.pool_size = 8;
    
    if (!RedisPool::instance().init(config)) {
        LOG_ERROR("Redis 连接池初始化失败");
        return 1;
    }
    LOG_INFO("Redis 连接池初始化成功");
    
    // ==================== 设置信号处理 ====================
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // ==================== 创建监听 socket ====================
    
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        LOG_ERROR("创建 socket 失败");
        return 1;
    }
    
    // 设置 SO_REUSEADDR（快速重启）
    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // 绑定地址
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_port);
    
    if (bind(g_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("绑定端口 %d 失败: %s", g_port, strerror(errno));
        close(g_server_fd);
        return 1;
    }
    
    // 开始监听
    if (listen(g_server_fd, 128) < 0) {
        LOG_ERROR("监听失败: %s", strerror(errno));
        close(g_server_fd);
        return 1;
    }
    
    LOG_INFO("服务器监听端口 %d", g_port);
    
    // ==================== 事件循环 ====================
    
    // 客户端连接信息
    struct ClientInfo {
        int fd;
        bool is_ws;
        int user_id;
        char buf[8192];
        int buf_len;
        std::string ws_buf;  // WebSocket 缓冲区
    };
    
    std::map<int, ClientInfo> clients;
    
    while (g_running) {
        // 构建 pollfd 数组
        std::vector<struct pollfd> fds;
        
        struct pollfd server_poll;
        server_poll.fd = g_server_fd;
        server_poll.events = POLLIN;
        server_poll.revents = 0;
        fds.push_back(server_poll);
        
        for (auto& kv : clients) {
            struct pollfd p;
            p.fd = kv.second.fd;
            p.events = POLLIN;
            p.revents = 0;
            fds.push_back(p);
        }
        
        // 等待事件（超时 100ms）
        int n = poll(fds.data(), fds.size(), 100);
        
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        
        // 检查监听 socket
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            
            int client_fd = accept(g_server_fd, 
                                  (struct sockaddr*)&client_addr, 
                                  &addr_len);
            
            if (client_fd >= 0) {
                // 设置 TCP_NODELAY（禁用 Nagle 算法）
                int flag = 1;
                setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
                
                // 添加到客户端列表
                ClientInfo info;
                info.fd = client_fd;
                info.is_ws = false;
                info.user_id = 0;
                info.buf_len = 0;
                clients[client_fd] = info;
                
                LOG_INFO("新连接: fd=%d, ip=%s", 
                        client_fd, 
                        inet_ntoa(client_addr.sin_addr));
            }
        }
        
        // 处理客户端数据
        for (size_t i = 1; i < fds.size() && i <= clients.size(); i++) {
            if (!(fds[i].revents & POLLIN)) continue;
            
            // 找到对应的 client
            int fd = fds[i].fd;
            auto it = clients.find(fd);
            if (it == clients.end()) continue;
            
            ClientInfo& client = it->second;
            
            // 读取数据
            char buf[4096];
            int n = read(fd, buf, sizeof(buf) - 1);
            
            if (n <= 0) {
                // 连接关闭
                LOG_INFO("连接关闭: fd=%d, user_id=%d", fd, client.user_id);
                ConnectionManager::instance().removeConnection(fd);
                close(fd);
                clients.erase(it);
                continue;
            }
            
            buf[n] = '\0';
            
            if (client.is_ws) {
                // WebSocket 模式：解析帧
                client.ws_buf.append(buf, n);
                
                while (true) {
                    int frame_len = 0;
                    std::string msg = decode_ws_frame(
                        client.ws_buf.c_str(), 
                        client.ws_buf.length(),
                        frame_len
                    );
                    
                    if (frame_len == 0) break;  // 数据不完整
                    
                    if (msg == "CLOSE") {
                        // 关闭连接
                        LOG_INFO("WebSocket 关闭: fd=%d", fd);
                        ConnectionManager::instance().removeConnection(fd);
                        close(fd);
                        clients.erase(it);
                        break;
                    }
                    
                    if (!msg.empty()) {
                        // 处理消息
                        handle_ws_message(fd, client.user_id, msg);
                    }
                    
                    // 移除已处理的帧
                    client.ws_buf.erase(0, frame_len);
                }
            } else {
                // HTTP 模式：检查是否 WebSocket 握手
                client.buf_len += n;
                
                std::string method, path, ws_key;
                int type = parse_http_request(client.buf, client.buf_len,
                                             method, path, ws_key);
                
                if (type == 1) {
                    // WebSocket 握手
                    LOG_INFO("WebSocket 握手: fd=%d, path=%s", fd, path.c_str());
                    
                    // 读取 body（可能有认证信息）
                    std::string body = read_http_body(client.buf, client.buf_len);
                    
                    // 验证 token（简化版：从 body 或 header 获取 user_id）
                    // 实际应该验证 JWT token
                    int user_id = 0;
                    if (!body.empty()) {
                        Json::Value root;
                        Json::Reader reader;
                        if (reader.parse(body, root)) {
                            user_id = root["user_id"].asInt();
                        }
                    }
                    
                    // 如果没有 user_id，分配一个临时 ID
                    if (user_id == 0) {
                        user_id = fd;  // 临时用 fd 作为 ID
                    }
                    
                    // 添加连接
                    ConnectionManager::instance().addConnection(fd);
                    ConnectionManager::instance().bindUser(fd, user_id);
                    
                    client.user_id = user_id;
                    client.is_ws = true;
                    
                    // 发送握手响应
                    std::string accept_key = compute_accept_key(ws_key);
                    
                    std::ostringstream resp;
                    resp << "HTTP/1.1 101 Switching Protocols\r\n";
                    resp << "Upgrade: websocket\r\n";
                    resp << "Connection: Upgrade\r\n";
                    resp << "Sec-WebSocket-Accept: " << accept_key << "\r\n";
                    resp << "\r\n";
                    
                    send(fd, resp.str().c_str(), resp.str().length(), 0);
                    
                    LOG_INFO("WebSocket 握手完成: fd=%d, user_id=%d", fd, user_id);
                    
                } else if (type == 0) {
                    // 普通 HTTP 请求
                    std::string body = read_http_body(client.buf, client.buf_len);
                    handle_http_request(fd, method, path, body);
                    
                    // HTTP 请求后关闭
                    close(fd);
                    clients.erase(it);
                    
                } else {
                    // 不完整的请求，继续读取
                }
            }
        }
    }
    
    // ==================== 清理 ====================
    
    LOG_INFO("游戏服务退出");
    
    // 关闭所有客户端连接
    for (auto& kv : clients) {
        close(kv.second.fd);
    }
    
    if (g_server_fd >= 0) {
        close(g_server_fd);
    }
    
    return 0;
}
