/**
 * @file http_client.cpp
 * @brief HTTP 客户端实现
 * 
 * 基于 libcurl（C 语言的 HTTP 客户端库）
 */

#include "http_client.h"
#include "logger.h"
#include <curl/curl.h>  // libcurl 头文件
#include <sstream>      // std::ostringstream
#include <cstring>      // strstr

// =====================================================
// 第一部分：构造和析构
// =====================================================

/**
 * @brief 构造函数
 */
HttpClient::HttpClient() {
    // 初始化 libcurl
    // curl_global_init() 只需要调用一次
    // CURL_GLOBAL_DEFAULT 通常足够
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // 创建 easy handle
    // easy handle 用于单线程的同步请求
    // 对应的是 multi handle（用于异步多请求）
    curl_ = curl_easy_init();
    
    if (curl_) {
        // 设置默认选项
        
        // 1. 自动重定向
        // 当收到 301/302 等重定向响应时
        // 自动跟随跳转（最多 10 次）
        curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
        
        // 2. 忽略 signals
        // CURLOPT_NOSIGNAL = 1 表示不让 libcurl 安装信号处理器
        // 这对于多线程环境很重要
        curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
        
        // 3. 启用 SSL 证书验证（默认）
        // 生产环境应该保持这个开启
        // curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
        // curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
        
        // 4. 默认超时
        setTimeout(timeout_ms_);
    }
}

/**
 * @brief 析构函数
 */
HttpClient::~HttpClient() {
    if (curl_) {
        // 清理 easy handle
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
    
    // 全局清理
    // 只在程序退出时调用一次
    curl_global_cleanup();
}

// =====================================================
// 第二部分：超时设置
// =====================================================

/**
 * @brief 设置超时时间
 * @param timeout_ms 超时时间（毫秒）
 */
void HttpClient::setTimeout(long timeout_ms) {
    timeout_ms_ = timeout_ms;
    
    if (curl_) {
        // CURLOPT_TIMEOUT_MS: 整个操作的超时时间
        // CURLOPT_CONNECTTIMEOUT_MS: 连接建立的超时时间
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms / 2);
    }
}

// =====================================================
// 第三部分：GET 请求
// =====================================================

/**
 * @brief GET 请求
 */
HttpResponse HttpClient::get(const std::string& url,
                            const std::map<std::string, std::string>& headers) {
    // GET 请求没有请求体
    return request("GET", url, "", headers);
}

// =====================================================
// 第四部分：POST 请求
// =====================================================

/**
 * @brief POST 请求
 */
HttpResponse HttpClient::post(const std::string& url, const std::string& body,
                              const std::map<std::string, std::string>& headers) {
    return request("POST", url, body, headers);
}

// =====================================================
// 第五部分：PUT 请求
// =====================================================

/**
 * @brief PUT 请求
 */
HttpResponse HttpClient::put(const std::string& url, const std::string& body,
                             const std::map<std::string, std::string>& headers) {
    return request("PUT", url, body, headers);
}

// =====================================================
// 第六部分：DELETE 请求
// =====================================================

/**
 * @brief DELETE 请求
 */
HttpResponse HttpClient::del(const std::string& url,
                             const std::map<std::string, std::string>& headers) {
    return request("DELETE", url, "", headers);
}

// =====================================================
// 第七部分：统一的请求处理 ⭐核心
// =====================================================

/**
 * @brief 统一的请求处理函数
 * 
 * 这是 HTTP 客户端的核心
 * 所有 HTTP 方法（GET/POST/PUT/DELETE）最终都调用这个函数
 * 
 * 流程图：
 * 
 *   ┌─────────────────────────────────────────────────────────┐
 *   │  1. 设置 URL                                            │
 *   │     curl_easy_setopt(CURLOPT_URL, url)                 │
 *   │                                                         │
 *   │  2. 设置 Header（如果有）                                │
 *   │     curl_slist_append() 构建链表                         │
 *   │     curl_easy_setopt(CURLOPT_HTTPHEADER, headers)       │
 *   │                                                         │
 *   │  3. 设置 Method 和 Body                                 │
 *   │     POST/PUT: 设置 CURLOPT_POSTFIELDS                   │
 *   │     GET/DELETE: 设置 CURLOPT_CUSTOMREQUEST              │
 *   │                                                         │
 *   │  4. 设置回调函数                                        │
 *   │     CURLOPT_WRITEFUNCTION: 接收响应体                   │
 *   │     CURLOPT_HEADERFUNCTION: 接收响应头                   │
 *   │                                                         │
 *   │  5. 执行请求                                            │
 *   │     curl_easy_perform()                                 │
 *   │                                                         │
 *   │  6. 获取结果                                            │
 *   │     curl_easy_getinfo(): 状态码、Content-Type 等       │
 *   │                                                         │
 *   │  7. 清理                                                │
 *   │     curl_slist_free_all(): 释放 Header 链表            │
 *   └─────────────────────────────────────────────────────────┘
 */
HttpResponse HttpClient::request(const std::string& method, const std::string& url,
                                 const std::string& body,
                                 const std::map<std::string, std::string>& headers) {
    HttpResponse response;
    
    if (!curl_) {
        response.error = "curl not initialized";
        return response;
    }

    // 用于存储响应的结构
    struct ResponseData {
        std::string body;
        std::map<std::string, std::string> headers;
    } resp_data;

    // =====================================================
    // Step 1: 重置 easy handle
    // =====================================================
    curl_easy_reset(curl_);

    // =====================================================
    // Step 2: 设置 URL
    // =====================================================
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    
    // 启用重定向
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    
    // 忽略信号（多线程安全）
    curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
    
    // 超时设置
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, timeout_ms_);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms_ / 2);

    // =====================================================
    // Step 3: 设置 Header
    // =====================================================
    struct curl_slist* header_list = nullptr;
    
    for (const auto& h : headers) {
        // 格式：Header-Name: Header-Value
        std::string header = h.first + ": " + h.second;
        header_list = curl_slist_append(header_list, header.c_str());
    }
    
    // 设置自定义 Header
    if (header_list) {
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, header_list);
    }

    // =====================================================
    // Step 4: 设置 Method
    // =====================================================
    if (method == "POST") {
        // POST 方法
        curl_easy_setopt(curl_, CURLOPT_POST, 1L);
        // 设置请求体
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
        // POSTFIELDSIZE 自动计算长度
        // 如果是二进制数据，用 CURLOPT_POSTFIELDSIZE_LARGE
        
    } else if (method == "PUT") {
        // PUT 方法
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
        
    } else if (method == "DELETE") {
        // DELETE 方法
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "DELETE");
        
    } else if (method == "GET") {
        // GET 是默认方法，不需要特殊设置
        // 但如果是重定向后的请求，需要允许自动重定向
        // 默认已经开启 CURLOPT_FOLLOWLOCATION
    }

    // =====================================================
    // Step 5: 设置回调函数
    // =====================================================
    
    // 响应体写入回调
    // libcurl 每收到一段数据就调用一次这个函数
    // 我们把数据追加到 resp_data.body
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &resp_data);
    
    // 响应头写入回调
    // libcurl 每收到一行响应头就调用一次
    // 我们解析 key: value 格式
    curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl_, CURLOPT_HEADERDATA, &resp_data);

    // =====================================================
    // Step 6: 执行请求
    // =====================================================
    CURLcode code = curl_easy_perform(curl_);
    
    if (code != CURLE_OK) {
        // 请求失败！
        response.error = curl_easy_strerror(code);
        response.success = false;
        LOG_ERROR("HTTP 请求失败: %s - %s", method.c_str(), response.error.c_str());
    } else {
        // 请求成功！
        response.success = true;
        
        // 获取 HTTP 状态码
        long status_code = 0;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &status_code);
        response.status_code = (int)status_code;
        
        // 获取响应体
        response.body = resp_data.body;
        
        // 获取响应头
        response.headers = resp_data.headers;
    }

    // =====================================================
    // Step 7: 清理
    // =====================================================
    if (header_list) {
        curl_slist_free_all(header_list);
    }

    return response;
}

// =====================================================
// 第八部分：回调函数 ⭐
// =====================================================

/**
 * @brief 响应体写入回调
 * 
 * libcurl 每收到一段数据就调用一次这个函数
 * 
 * @param contents 写入的数据指针
 * @param size 每个元素的大小（通常为 1）
 * @param nmemb 元素数量
 * @param userp 用户指针（我们传的是 ResponseData*）
 * @return 写入的字节数（必须是 size * nmemb）
 * 
 * 注意：这个函数可能被调用多次！
 *       每次调用只收到一小段数据，需要累积起来
 */
size_t HttpClient::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    // 计算实际数据大小
    size_t realsize = size * nmemb;
    
    // 转换为 ResponseData 指针
    auto* resp_data = static_cast<struct ResponseData*>(userp);
    
    // 追加数据到 body
    // append(const char*, size_t) 是安全的方式
    // 不会遇到字符串中间有 \0 的问题（二进制数据）
    resp_data->body.append(static_cast<char*>(contents), realsize);
    
    // 返回处理的字节数
    // 如果返回的值不等于 size * nmemb
    // libcurl 会认为出错了
    return realsize;
}

/**
 * @brief 响应头写入回调
 * 
 * HTTP 响应头的格式：
 *   Header-Name: Header-Value\r\n
 * 
 * 例如：
 *   Content-Type: application/json\r\n
 *   Content-Length: 1234\r\n
 *   Set-Cookie: session=abc123\r\n
 * 
 * @param contents 一行响应头
 * @param size 每个元素的大小
 * @param nmemb 元素数量
 * @param userp 用户指针
 * @return 处理的字节数
 */
size_t HttpClient::headerCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    
    auto* resp_data = static_cast<struct ResponseData*>(userp);
    
    // 转换为字符串
    std::string header(static_cast<char*>(contents), realsize);
    
    // 找到冒号的位置
    size_t colon_pos = header.find(':');
    if (colon_pos != std::string::npos) {
        // 提取键和值
        std::string key = header.substr(0, colon_pos);
        std::string value = header.substr(colon_pos + 1);
        
        // 去除首尾空白和 \r\n
        // trim 操作
        while (!key.empty() && isspace(key.back())) key.pop_back();
        while (!key.empty() && isspace(key.front())) key.erase(key.begin());
        while (!value.empty() && isspace(value.back())) value.pop_back();
        while (!value.empty() && isspace(value.front())) value.erase(value.begin());
        
        // 存储到 map
        if (!key.empty()) {
            resp_data->headers[key] = value;
        }
    }
    
    return realsize;
}

// =====================================================
// 第九部分：异步请求
// =====================================================

/**
 * @brief 异步 GET
 */
void HttpClient::asyncGet(const std::string& url, Callback callback,
                          void* thread_pool) {
    // TODO: 使用线程池执行同步请求
    // 目前只是同步执行然后调用回调
    auto response = get(url);
    if (callback) {
        callback(response);
    }
}

/**
 * @brief 异步 POST
 */
void HttpClient::asyncPost(const std::string& url, const std::string& body,
                           Callback callback, void* thread_pool) {
    auto response = post(url, body);
    if (callback) {
        callback(response);
    }
}

// =====================================================
// 第十部分：深入理解
// =====================================================

/*
┌─────────────────────────────────────────────────────────────────┐
│                    libcurl 核心概念详解                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. easy handle vs multi handle                                  │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  easy handle:                                          │   │
│  │    - 单线程同步请求                                     │   │
│  │    - 简单，适合大多数场景                                │   │
│  │    - curl_easy_init() → ... → curl_easy_perform()     │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  multi handle:                                         │   │
│  │    - 多线程异步请求                                     │   │
│  │    - 复杂，但效率更高                                    │   │
│  │    - curl_multi_init() → 添加 easy → select/poll → ...  │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
│  2. CURLOPT_* 选项                                              │
│                                                                 │
│  libcurl 通过 curl_easy_setopt() 设置各种选项：                  │
│  - CURLOPT_URL: 请求的 URL                                      │
│  - CURLOPT_TIMEOUT_MS: 超时时间                                 │
│  - CURLOPT_WRITEFUNCTION: 响应体写入回调                         │
│  - CURLOPT_POSTFIELDS: POST 请求体                              │
│  - ... 上百个选项！                                             │
│                                                                 │
│  3. 回调函数的注意事项                                          │
│                                                                 │
│  - 回调函数可能被调用多次（分多次接收数据）                       │
│  - 必须累计处理，不能只处理最后一次                               │
│  - 必须返回实际处理的字节数                                      │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
*/

// =====================================================
// 使用示例（注释）
// =====================================================
/*
// 示例 1：简单的 GET 请求
void example1() {
    HttpClient client;
    client.setTimeout(3000);  // 3秒超时
    
    auto resp = client.get("http://httpbin.org/get");
    
    if (resp.success) {
        std::cout << "状态码: " << resp.status_code << std::endl;
        std::cout << "响应体: " << resp.body << std::endl;
    } else {
        std::cout << "错误: " << resp.error << std::endl;
    }
}

// 示例 2：POST JSON 数据
void example2() {
    HttpClient client;
    
    std::string json = R"({
        "username": "alice",
        "password": "123456"
    })";
    
    auto resp = client.post(
        "http://httpbin.org/post",
        json,
        {{"Content-Type", "application/json"}}  // 设置 Header
    );
    
    std::cout << resp.body << std::endl;
}

// 示例 3：带认证的请求
void example3() {
    HttpClient client;
    
    auto resp = client.get(
        "http://api.example.com/protected",
        {{"Authorization", "Bearer your-token-here"}}  // Bearer Token
    );
    
    if (resp.status_code == 401) {
        std::cout << "需要登录！" << std::endl;
    }
}

// 示例 4：处理不同状态码
void example4() {
    HttpClient client;
    auto resp = client.get("http://httpbin.org/status/404");
    
    switch (resp.status_code) {
        case 200:
            std::cout << "成功" << std::endl;
            break;
        case 404:
            std::cout << "资源不存在" << std::endl;
            break;
        case 500:
            std::cout << "服务器错误" << std::endl;
            break;
        default:
            std::cout << "其他错误: " << resp.status_code << std::endl;
    }
}

// 示例 5：使用响应头
void example5() {
    HttpClient client;
    auto resp = client.get("http://httpbin.org/headers");
    
    // 遍历响应头
    for (const auto& h : resp.headers) {
        std::cout << h.first << ": " << h.second << std::endl;
    }
    
    // 常见响应头
    if (resp.headers.count("Content-Type")) {
        std::cout << "数据类型: " << resp.headers["Content-Type"] << std::endl;
    }
}

// 示例 6：文件下载（需要稍微修改回调）
void example6() {
    HttpClient client;
    
    // 注意：这里需要修改回调函数，将数据写入文件
    // writeCallback 改为 fwrite()
    
    auto resp = client.get("http://example.com/file.pdf");
    
    if (resp.success) {
        // 写入文件
        std::ofstream out("file.pdf", std::ios::binary);
        out.write(resp.body.data(), resp.body.size());
        out.close();
    }
}
*/
