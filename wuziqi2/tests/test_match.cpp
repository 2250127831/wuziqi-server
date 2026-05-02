#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <json/json.h>
#include <random>
#include <sstream>

// HTTP 请求封装
std::string buildHttpRequest(const std::string& method, const std::string& path,
                             const Json::Value& body) {
    Json::StreamWriterBuilder builder;
    std::string body_str = Json::writeString(builder, body);

    std::string request = method + " " + path + " HTTP/1.1\r\n";
    request += "Host: localhost:8002\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + std::to_string(body_str.length()) + "\r\n";
    request += "Connection: close\r\n";
    request += "\r\n";
    request += body_str;
    return request;
}

// 发送请求并获取响应
bool sendRequest(const std::string& host, int port, const std::string& request,
                 std::string& response) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "创建 socket 失败" << std::endl;
        return false;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "连接服务器失败: " << strerror(errno) << std::endl;
        close(sock);
        return false;
    }

    ssize_t sent = send(sock, request.c_str(), request.length(), 0);
    if (sent != (ssize_t)request.length()) {
        std::cerr << "发送请求失败" << std::endl;
        close(sock);
        return false;
    }

    // 接收响应
    char buffer[8192];
    std::string full_response;
    while (true) {
        ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        buffer[n] = '\0';
        full_response += buffer;
        if (n < (ssize_t)(sizeof(buffer) - 1)) break;
    }

    close(sock);
    response = full_response;
    return true;
}

// 解析 HTTP 响应
bool parseHttpResponse(const std::string& response, std::string& body) {
    size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        std::cerr << "无法找到 HTTP 头部结束位置" << std::endl;
        return false;
    }

    body = response.substr(header_end + 4);
    return true;
}

// 获取 HTTP 状态码
int getHttpStatusCode(const std::string& response) {
    size_t end = response.find("\r\n");
    if (end == std::string::npos) return 0;
    std::string status_line = response.substr(0, end);
    if (status_line.find("200") != std::string::npos) return 200;
    if (status_line.find("201") != std::string::npos) return 201;
    if (status_line.find("400") != std::string::npos) return 400;
    if (status_line.find("404") != std::string::npos) return 404;
    if (status_line.find("500") != std::string::npos) return 500;
    return 0;
}

// 解析 JSON 响应
bool parseJsonResponse(const std::string& body, Json::Value& json) {
    Json::CharReaderBuilder builder;
    std::istringstream stream(body);
    std::string err;
    return Json::parseFromStream(builder, stream, &json, &err);
}

// 生成随机用户ID
int generateRandomUserId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    return dis(gen);
}

void testCreateRoom(const std::string& host, int port) {
    std::cout << "\n========== 测试创建房间 ==========" << std::endl;

    int user_id = generateRandomUserId();
    Json::Value body;
    body["user_id"] = user_id;
    body["room_name"] = "测试房间_" + std::to_string(time(nullptr));
    body["max_players"] = 2;

    std::string request = buildHttpRequest("POST", "/api/create_room", body);
    std::string response;

    if (!sendRequest(host, port, request, response)) {
        std::cout << "[失败] 无法连接到服务" << std::endl;
        return;
    }

    std::string body_str;
    if (!parseHttpResponse(response, body_str)) {
        std::cout << "[失败] HTTP 响应异常" << std::endl;
        return;
    }

    int status = getHttpStatusCode(response);
    std::cout << "HTTP 状态码: " << status << std::endl;

    Json::Value json;
    if (parseJsonResponse(body_str, json)) {
        std::cout << "响应: " << json.toStyledString();
    } else {
        std::cout << "响应体(非JSON): " << body_str << std::endl;
    }

    if (status == 201 && json["success"].asBool()) {
        std::cout << "[通过] 房间创建成功! room_id: " << json["data"]["room_id"].asString() << std::endl;
    } else {
        std::cout << "[未通过] " << json["message"].asString() << std::endl;
    }
}

void testJoinRoom(const std::string& host, int port) {
    std::cout << "\n========== 测试加入房间 ==========" << std::endl;

    // 先创建一个房间
    int user_id1 = generateRandomUserId();
    Json::Value create_body;
    create_body["user_id"] = user_id1;
    create_body["room_name"] = "加入测试房间";
    create_body["max_players"] = 2;

    std::string create_request = buildHttpRequest("POST", "/api/create_room", create_body);
    std::string create_response;

    if (!sendRequest(host, port, create_request, create_response)) {
        std::cout << "[跳过] 无法连接服务创建房间" << std::endl;
        return;
    }

    std::string create_body_str;
    if (!parseHttpResponse(create_response, create_body_str)) {
        std::cout << "[跳过] 创建房间响应异常" << std::endl;
        return;
    }

    Json::Value create_json;
    if (!parseJsonResponse(create_body_str, create_json) || !create_json["success"].asBool()) {
        std::cout << "[跳过] 创建房间失败" << std::endl;
        return;
    }

    std::string room_id = create_json["data"]["room_id"].asString();
    std::cout << "创建的房间 room_id: " << room_id << std::endl;

    // 加入房间
    int user_id2 = generateRandomUserId();
    Json::Value join_body;
    join_body["user_id"] = user_id2;
    join_body["room_id"] = room_id;

    std::string join_request = buildHttpRequest("POST", "/api/join_room", join_body);
    std::string join_response;

    if (!sendRequest(host, port, join_request, join_response)) {
        std::cout << "[失败] 无法连接服务" << std::endl;
        return;
    }

    std::string join_body_str;
    if (!parseHttpResponse(join_response, join_body_str)) {
        std::cout << "[失败] HTTP 响应异常" << std::endl;
        return;
    }

    int status = getHttpStatusCode(join_response);
    std::cout << "HTTP 状态码: " << status << std::endl;

    Json::Value join_json;
    if (parseJsonResponse(join_body_str, join_json)) {
        std::cout << "响应: " << join_json.toStyledString();
    } else {
        std::cout << "响应体(非JSON): " << join_body_str << std::endl;
    }

    if (status == 200 && join_json["success"].asBool()) {
        std::cout << "[通过] 加入房间成功!" << std::endl;
    } else {
        std::cout << "[未通过] " << join_json["message"].asString() << std::endl;
    }
}

void testRandomMatch(const std::string& host, int port) {
    std::cout << "\n========== 测试随机匹配 ==========" << std::endl;

    // 需要至少两个玩家同时匹配才能配对成功
    // 这里只测试单人匹配请求
    int user_id = generateRandomUserId();
    Json::Value body;
    body["user_id"] = user_id;

    std::string request = buildHttpRequest("POST", "/api/random_match", body);
    std::string response;

    if (!sendRequest(host, port, request, response)) {
        std::cout << "[失败] 无法连接到服务" << std::endl;
        return;
    }

    std::string body_str;
    if (!parseHttpResponse(response, body_str)) {
        std::cout << "[失败] HTTP 响应异常" << std::endl;
        return;
    }

    int status = getHttpStatusCode(response);
    std::cout << "HTTP 状态码: " << status << std::endl;

    Json::Value json;
    if (parseJsonResponse(body_str, json)) {
        std::cout << "响应: " << json.toStyledString();
    } else {
        std::cout << "响应体(非JSON): " << body_str << std::endl;
    }

    if (status == 200 && json["success"].asBool()) {
        std::cout << "[通过] 随机匹配请求成功!" << std::endl;
    } else {
        std::cout << "[未通过] " << json["message"].asString() << std::endl;
    }
}

void testLeaveRoom(const std::string& host, int port) {
    std::cout << "\n========== 测试离开房间 ==========" << std::endl;

    // 先创建一个房间
    int user_id = generateRandomUserId();
    Json::Value create_body;
    create_body["user_id"] = user_id;
    create_body["room_name"] = "离开测试房间";
    create_body["max_players"] = 2;

    std::string create_request = buildHttpRequest("POST", "/api/create_room", create_body);
    std::string create_response;

    if (!sendRequest(host, port, create_request, create_response)) {
        std::cout << "[跳过] 无法连接服务" << std::endl;
        return;
    }

    std::string create_body_str;
    if (!parseHttpResponse(create_response, create_body_str)) {
        std::cout << "[跳过] 创建房间响应异常" << std::endl;
        return;
    }

    Json::Value create_json;
    if (!parseJsonResponse(create_body_str, create_json) || !create_json["success"].asBool()) {
        std::cout << "[跳过] 创建房间失败" << std::endl;
        return;
    }

    std::string room_id = create_json["data"]["room_id"].asString();
    std::cout << "创建的房间 room_id: " << room_id << std::endl;

    // 离开房间
    Json::Value leave_body;
    leave_body["user_id"] = user_id;
    leave_body["room_id"] = room_id;

    std::string leave_request = buildHttpRequest("POST", "/api/quit_room", leave_body);
    std::string leave_response;

    if (!sendRequest(host, port, leave_request, leave_response)) {
        std::cout << "[失败] 无法连接服务" << std::endl;
        return;
    }

    std::string leave_body_str;
    if (!parseHttpResponse(leave_response, leave_body_str)) {
        std::cout << "[失败] HTTP 响应异常" << std::endl;
        return;
    }

    int status = getHttpStatusCode(leave_response);
    std::cout << "HTTP 状态码: " << status << std::endl;

    Json::Value leave_json;
    if (parseJsonResponse(leave_body_str, leave_json)) {
        std::cout << "响应: " << leave_json.toStyledString();
    } else {
        std::cout << "响应体(非JSON): " << leave_body_str << std::endl;
    }

    if (status == 200 && leave_json["success"].asBool()) {
        std::cout << "[通过] 离开房间成功!" << std::endl;
    } else {
        std::cout << "[未通过] " << leave_json["message"].asString() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 8002;

    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    std::cout << "========================================" << std::endl;
    std::cout << "   match-service 测试程序" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "测试服务: " << host << ":" << port << std::endl;

    testCreateRoom(host, port);
    testJoinRoom(host, port);
    testRandomMatch(host, port);
    testLeaveRoom(host, port);

    std::cout << "\n========================================" << std::endl;
    std::cout << "   测试完成" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
