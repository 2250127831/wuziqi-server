#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <json/json.h>

// HTTP 请求封装
std::string buildHttpRequest(const std::string& method, const std::string& path, 
                             const Json::Value& body) {
    Json::StreamWriterBuilder builder;
    std::string body_str = Json::writeString(builder, body);
    
    std::string request = method + " " + path + " HTTP/1.1\r\n";
    request += "Host: localhost:8001\r\n";
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
        std::cerr << "连接服务器失败" << std::endl;
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
    char buffer[4096];
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
    
    // 解析状态码
    std::string status_line = response.substr(0, header_end);
    if (status_line.find("200") != std::string::npos) {
        return true;
    }
    return false;
}

// 解析 JSON 响应
bool parseJsonResponse(const std::string& body, Json::Value& json) {
    Json::CharReaderBuilder builder;
    std::istringstream stream(body);
    std::string err;
    return Json::parseFromStream(builder, stream, &json, &err);
}

void testRegister(const std::string& host, int port) {
    std::cout << "\n========== 测试用户注册 ==========" << std::endl;
    
    Json::Value body;
    body["username"] = "testuser_" + std::to_string(time(nullptr));
    body["password"] = "password123";
    body["email"] = "test@example.com";
    
    std::string request = buildHttpRequest("POST", "/api/register", body);
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
    
    Json::Value json;
    if (!parseJsonResponse(body_str, json)) {
        std::cout << "[失败] JSON 解析失败: " << body_str << std::endl;
        return;
    }
    
    std::cout << "请求: " << body.toStyledString();
    std::cout << "响应: " << json.toStyledString();
    
    if (json["success"].asBool()) {
        std::cout << "[通过] 用户注册成功! user_id: " << json["data"]["user_id"].asInt() << std::endl;
    } else {
        std::cout << "[未通过] " << json["message"].asString() << std::endl;
    }
}

void testLogin(const std::string& host, int port) {
    std::cout << "\n========== 测试用户登录 ==========" << std::endl;
    
    // 先注册一个用户
    Json::Value reg_body;
    std::string username = "logintest_" + std::to_string(time(nullptr));
    reg_body["username"] = username;
    reg_body["password"] = "password123";
    reg_body["email"] = "login@example.com";
    
    std::string reg_request = buildHttpRequest("POST", "/api/register", reg_body);
    std::string reg_response;
    
    if (!sendRequest(host, port, reg_request, reg_response)) {
        std::cout << "[跳过] 无法连接服务进行注册" << std::endl;
        return;
    }
    
    // 登录
    Json::Value login_body;
    login_body["username"] = username;
    login_body["password"] = "password123";
    
    std::string request = buildHttpRequest("POST", "/api/login", login_body);
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
    
    Json::Value json;
    if (!parseJsonResponse(body_str, json)) {
        std::cout << "[失败] JSON 解析失败" << std::endl;
        return;
    }
    
    std::cout << "登录请求: " << login_body.toStyledString();
    std::cout << "响应: " << json.toStyledString();
    
    if (json["success"].asBool()) {
        std::cout << "[通过] 用户登录成功! token: " << json["data"]["token"].asString() << std::endl;
    } else {
        std::cout << "[未通过] " << json["message"].asString() << std::endl;
    }
}

void testVerifyToken(const std::string& host, int port) {
    std::cout << "\n========== 测试 Token 验证 ==========" << std::endl;
    
    // 先注册并登录获取 token
    Json::Value reg_body;
    std::string username = "verifytest_" + std::to_string(time(nullptr));
    reg_body["username"] = username;
    reg_body["password"] = "password123";
    reg_body["email"] = "verify@example.com";
    
    std::string reg_request = buildHttpRequest("POST", "/api/register", reg_body);
    std::string reg_response;
    
    if (!sendRequest(host, port, reg_request, reg_response)) {
        std::cout << "[跳过] 无法连接服务" << std::endl;
        return;
    }
    
    // 登录获取 token
    Json::Value login_body;
    login_body["username"] = username;
    login_body["password"] = "password123";
    
    std::string login_request = buildHttpRequest("POST", "/api/login", login_body);
    std::string login_response;
    
    if (!sendRequest(host, port, login_request, login_response)) {
        std::cout << "[跳过] 无法连接服务" << std::endl;
        return;
    }
    
    std::string body_str;
    if (!parseHttpResponse(login_response, body_str)) {
        std::cout << "[失败] HTTP 响应异常" << std::endl;
        return;
    }
    
    Json::Value json;
    if (!parseJsonResponse(body_str, json)) {
        std::cout << "[失败] JSON 解析失败" << std::endl;
        return;
    }
    
    if (!json["success"].asBool()) {
        std::cout << "[跳过] 登录失败" << std::endl;
        return;
    }
    
    std::string token = json["data"]["token"].asString();
    std::cout << "获取到 token: " << token << std::endl;
    
    // 验证 token
    Json::Value verify_body;
    verify_body["token"] = token;
    
    std::string verify_request = buildHttpRequest("POST", "/api/verify", verify_body);
    std::string verify_response;
    
    if (!sendRequest(host, port, verify_request, verify_response)) {
        std::cout << "[失败] 无法连接服务" << std::endl;
        return;
    }
    
    if (!parseHttpResponse(verify_response, body_str)) {
        std::cout << "[失败] HTTP 响应异常" << std::endl;
        return;
    }
    
    if (!parseJsonResponse(body_str, json)) {
        std::cout << "[失败] JSON 解析失败" << std::endl;
        return;
    }
    
    std::cout << "Token 验证响应: " << json.toStyledString();
    
    if (json["success"].asBool()) {
        std::cout << "[通过] Token 验证成功! user_id: " << json["data"]["user_id"].asInt() << std::endl;
    } else {
        std::cout << "[未通过] " << json["message"].asString() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 8001;
    
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "   auth-service 测试程序" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "测试服务: " << host << ":" << port << std::endl;
    
    testRegister(host, port);
    testLogin(host, port);
    testVerifyToken(host, port);
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "   测试完成" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
