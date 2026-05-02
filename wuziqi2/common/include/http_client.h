/**
 * @file http_client.h
 * @brief HTTP 客户端封装
 * 
 * =====================================================
 * 什么是 HTTP？
 * =====================================================
 * 
 * HTTP 是"超文本传输协议"，是浏览器和服务器通信的基础
 * 
 * 常见的 HTTP 方法：
 * - GET: 获取资源（如打开网页）
 * - POST: 提交数据（如登录表单）
 * - PUT: 更新资源
 * - DELETE: 删除资源
 * 
 * =====================================================
 * 什么是 libcurl？
 * =====================================================
 * 
 * libcurl 是一个 C 语言的 HTTP 客户端库
 * - 支持多种协议（HTTP, HTTPS, FTP 等）
 * - 支持 GET, POST, PUT, DELETE 等方法
 * - 支持 HTTPS（加密）
 * - 跨平台
 * 
 * 这个类封装了 libcurl，提供简洁的 C++ 接口
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>          // std::string
#include <map>            // std::map
#include <functional>      // std::function
#include <memory>         // std::unique_ptr

// =====================================================
// 第一部分：HTTP 响应结构
// =====================================================
/**
 * @struct HttpResponse
 * @brief HTTP 响应数据结构
 * 
 * 当你访问一个 URL，服务器会返回：
 * - 状态码（如 200 表示成功）
 * - 响应体（HTML、JSON 等内容）
 * - 响应头（服务器信息、Cookie 等）
 */
struct HttpResponse {
    int status_code = 0;                              // HTTP 状态码
    std::string body;                                  // 响应体（主要数据）
    std::map<std::string, std::string> headers;       // 响应头
    bool success = false;                             // 请求是否成功
    std::string error;                                // 错误信息

    /**
     * @brief 判断是否成功
     * @return true 如果状态码在 200-299 范围内
     */
    bool isOK() const { return success && status_code >= 200 && status_code < 300; }
};

// =====================================================
// 第二部分：HttpClient 类
// =====================================================
/**
 * @class HttpClient
 * @brief HTTP 客户端
 * 
 * 基于 libcurl 实现
 * 
 * 使用示例：
 * @code
 *   HttpClient client;
 *   client.setTimeout(3000);  // 3秒超时
 *   
 *   // GET 请求
 *   auto resp = client.get("http://api.example.com/users/1");
 *   if (resp.success) {
 *       std::cout << resp.body << std::endl;
 *   }
 *   
 *   // POST 请求
 *   auto resp = client.post("http://api.example.com/users",
 *       R"({"name":"alice","age":20})",
 *       {{"Content-Type", "application/json"}});
 * @endcode
 */
class HttpClient {
public:
    /**
     * @brief 构造函数
     */
    HttpClient();
    
    /**
     * @brief 析构函数
     */
    ~HttpClient();

    // =====================================================
    // 超时设置
    // =====================================================
    /**
     * @brief 设置超时时间
     * @param timeout_ms 超时时间（毫秒）
     * 
     * 包括：
     * - 连接超时：建立 TCP 连接的时间
     * - 读取超时：等待服务器响应的时间
     */
    void setTimeout(long timeout_ms);

    // =====================================================
    // HTTP 方法
    // =====================================================
    /**
     * @brief GET 请求
     * @param url 请求 URL
     * @param headers 自定义请求头（可选）
     * @return HTTP 响应
     * 
     * GET 用于获取数据
     * 参数通常放在 URL 中：/api/users?id=1
     * 
     * 示例：
     *   client.get("http://api.example.com/users/1");
     */
    HttpResponse get(const std::string& url,
                     const std::map<std::string, std::string>& headers = {});

    /**
     * @brief POST 请求
     * @param url 请求 URL
     * @param body 请求体（如 JSON）
     * @param headers 自定义请求头
     * @return HTTP 响应
     * 
     * POST 用于提交数据
     * 常用于创建资源、登录等
     * 
     * 示例：
     *   client.post("http://api.example.com/login",
     *       R"({"username":"alice","password":"123"})",
     *       {{"Content-Type", "application/json"}});
     */
    HttpResponse post(const std::string& url,
                      const std::string& body = "",
                      const std::map<std::string, std::string>& headers = {});

    /**
     * @brief PUT 请求
     * @param url 请求 URL
     * @param body 请求体
     * @param headers 自定义请求头
     * @return HTTP 响应
     * 
     * PUT 用于更新整个资源
     */
    HttpResponse put(const std::string& url,
                      const std::string& body = "",
                      const std::map<std::string, std::string>& headers = {});

    /**
     * @brief DELETE 请求
     * @param url 请求 URL
     * @param headers 自定义请求头
     * @return HTTP 响应
     * 
     * DELETE 用于删除资源
     */
    HttpResponse del(const std::string& url,
                     const std::map<std::string, std::string>& headers = {});

    // =====================================================
    // 异步请求
    // =====================================================
    /**
     * @brief 异步 GET 请求
     * @param url 请求 URL
     * @param callback 回调函数（请求完成时调用）
     * @param thread_pool 线程池（可选）
     * 
     * 异步的好处：
     * - 不阻塞当前线程
     * - 可以同时发起多个请求
     */
    using Callback = std::function<void(const HttpResponse&)>;
    
    void asyncGet(const std::string& url, Callback callback,
                  void* thread_pool = nullptr);
    
    void asyncPost(const std::string& url, const std::string& body,
                   Callback callback, void* thread_pool = nullptr);

// =====================================================
// 私有成员
// =====================================================
private:
    /**
     * @brief 统一的请求处理
     * @param method HTTP 方法（GET/POST/PUT/DELETE）
     * @param url 请求 URL
     * @param body 请求体
     * @param headers 请求头
     * @return HTTP 响应
     * 
     * 所有 HTTP 方法最终都调用这个函数
     */
    HttpResponse request(const std::string& method, const std::string& url,
                         const std::string& body = "",
                         const std::map<std::string, std::string>& headers = {});

    /**
     * @brief 响应体写入回调
     * @param contents 写入的数据
     * @param size 每个元素的大小
     * @param nmemb 元素数量
     * @param userp 用户指针（指向 ResponseData）
     * @return 处理的字节数
     * 
     * libcurl 接收数据时会调用这个函数
     * 用于将数据追加到响应体
     */
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);

    /**
     * @brief 响应头写入回调
     * @param contents 写入的数据
     * @param size 每个元素的大小
     * @param nmemb 元素数量
     * @param userp 用户指针
     * @return 处理的字节数
     * 
     * 解析 HTTP 响应头
     * 提取 Content-Type、Set-Cookie 等信息
     */
    static size_t headerCallback(void* contents, size_t size, size_t nmemb, void* userp);

    long timeout_ms_ = 3000;    // 超时时间（毫秒）
    void* curl_ = nullptr;       // CURL* 句柄（libcurl 的核心类型）
};

// =====================================================
// HTTP 状态码参考
// =====================================================
/*
┌─────────────────────────────────────────────────────────────────┐
│                      常见 HTTP 状态码                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  2xx 成功                                                        │
│  ├── 200 OK                    请求成功                          │
│  ├── 201 Created              资源创建成功                        │
│  ├── 204 No Content           请求成功，无返回内容                │
│                                                                 │
│  3xx 重定向                                                      │
│  ├── 301 Moved Permanently    永久重定向                          │
│  ├── 302 Found               临时重定向                          │
│  ├── 304 Not Modified         缓存未过期                          │
│                                                                 │
│  4xx 客户端错误                                                  │
│  ├── 400 Bad Request          请求格式错误                        │
│  ├── 401 Unauthorized         未认证（需要登录）                  │
│  ├── 403 Forbidden            无权限                              │
│  ├── 404 Not Found            资源不存在                          │
│  ├── 422 Unprocessable Entity 请求数据格式正确但语义错误         │
│  └── 429 Too Many Requests    请求过于频繁                        │
│                                                                 │
│  5xx 服务器错误                                                  │
│  ├── 500 Internal Server Error 服务器内部错误                    │
│  ├── 502 Bad Gateway          网关错误                            │
│  ├── 503 Service Unavailable 服务不可用                          │
│  └── 504 Gateway Timeout      网关超时                            │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
*/

#endif // HTTP_CLIENT_H
