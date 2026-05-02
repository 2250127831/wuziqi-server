/**
 * @file utils.h
 * @brief 工具函数集合
 * 
 * =====================================================
 * 这个文件包含什么？
 * =====================================================
 * 
 * 一些常用的工具函数：
 * - 字符串处理（trim、split、replace）
 * - JSON 解析
 * - Base64 编解码
 * - UUID 生成
 * - 时间相关
 * - 文件相关
 * 
 * 这些是"工具函数"，不涉及具体的业务逻辑
 * 可以在任何地方复用
 */

#ifndef UTILS_H
#define UTILS_H

#include <string>          // std::string
#include <vector>          // std::vector
#include <map>             // std::map
#include <unordered_map>   // std::unordered_map
#include <ctime>           // time_t
#include <cstdint>        // uint64_t

// =====================================================
// 第一部分：字符串处理
// =====================================================
/**
 * @brief 去除字符串首尾的空白字符
 * @param str 输入字符串
 * @return 去除空白后的字符串
 * 
 * 空白字符包括：空格、\t、\n、\r
 * 
 * 示例：
 *   trim("  hello  ")  → "hello"
 *   trim("\tworld\n")   → "world"
 */
std::string trim(const std::string& str);

/**
 * @brief 去除字符串左边的空白
 */
std::string trimLeft(const std::string& str);

/**
 * @brief 去除字符串右边的空白
 */
std::string trimRight(const std::string& str);

/**
 * @brief 字符串分割
 * @param str 输入字符串
 * @param delimiter 分隔符
 * @return 分割后的字符串数组
 * 
 * 示例：
 *   split("a,b,c", ",")  → ["a", "b", "c"]
 *   split("hello world", " ")  → ["hello", "world"]
 */
std::vector<std::string> split(const std::string& str, const std::string& delimiter);

/**
 * @brief 字符串替换
 * @param str 输入字符串
 * @param from 要替换的字符串
 * @param to 替换成的字符串
 * @return 替换后的字符串
 * 
 * 示例：
 *   replace("hello world", "world", "C++")  → "hello C++"
 */
std::string replace(const std::string& str, const std::string& from, const std::string& to);

/**
 * @brief 字符串转大写
 */
std::string toUpper(const std::string& str);

/**
 * @brief 字符串转小写
 */
std::string toLower(const std::string& str);

/**
 * @brief 判断字符串是否以指定前缀开始
 */
bool startsWith(const std::string& str, const std::string& prefix);

/**
 * @brief 判断字符串是否以指定后缀结束
 */
bool endsWith(const std::string& str, const std::string& suffix);

// =====================================================
// 第二部分：JSON 处理
// =====================================================
/**
 * @brief JSON 值的类型
 */
enum class JsonType {
    STRING,
    NUMBER,
    OBJECT,
    ARRAY,
    BOOLEAN,
    NULL_TYPE,
};

/**
 * @class JsonValue
 * @brief JSON 值
 * 
 * 简化的 JSON 解析器
 * 只支持基本的 JSON 类型
 */
class JsonValue {
public:
    JsonType type;
    
    // 根据类型的值存储
    std::string str_value;                    // 字符串值
    double num_value = 0;                      // 数值
    bool bool_value = false;                  // 布尔值
    std::map<std::string, JsonValue> obj;     // 对象
    std::vector<JsonValue> arr;               // 数组
    
    /**
     * @brief 获取字符串值
     */
    const std::string& asString() const { return str_value; }
    
    /**
     * @brief 获取数值
     */
    double asNumber() const { return num_value; }
    
    /**
     * @brief 获取布尔值
     */
    bool asBool() const { return bool_value; }
    
    /**
     * @brief 获取对象属性
     */
    const JsonValue& operator[](const std::string& key) const;
    
    /**
     * @brief 获取数组元素
     */
    const JsonValue& operator[](size_t index) const;
    
    /**
     * @brief 获取数组大小
     */
    size_t size() const;
    
    /**
     * @brief 判断是否为 null
     */
    bool isNull() const { return type == JsonType::NULL_TYPE; }
    
    /**
     * @brief 判断是否为对象
     */
    bool isObject() const { return type == JsonType::OBJECT; }
    
    /**
     * @brief 判断是否为数组
     */
    bool isArray() const { return type == JsonType::ARRAY; }
};

/**
 * @class Json
 * @brief JSON 解析和构建
 */
class Json {
public:
    /**
     * @brief 解析 JSON 字符串
     * @param json_str JSON 字符串
     * @return 解析后的 JsonValue
     * 
     * 示例：
     *   Json::parse(R"({"name": "alice", "age": 20})");
     */
    static JsonValue parse(const std::string& json_str);
    
    /**
     * @brief 序列化 JsonValue 为字符串
     * @param value JsonValue
     * @param pretty 是否格式化输出
     * @return JSON 字符串
     */
    static std::string stringify(const JsonValue& value, bool pretty = false);
    
    /**
     * @brief 创建字符串值
     */
    static JsonValue string(const std::string& value);
    
    /**
     * @brief 创建数值
     */
    static JsonValue number(double value);
    
    /**
     * @brief 创建布尔值
     */
    static JsonValue boolean(bool value);
    
    /**
     * @brief 创建 null
     */
    static JsonValue null();
    
    /**
     * @brief 创建对象
     */
    static JsonValue object();
    
    /**
     * @brief 创建数组
     */
    static JsonValue array();
};

// =====================================================
// 第三部分：Base64 编解码
// =====================================================
/**
 * @brief Base64 编码
 * @param data 输入数据
 * @return Base64 编码字符串
 * 
 * Base64 将二进制数据编码为 ASCII 字符串
 * 常用于：
 * - 邮件附件（MIME）
 * - URL 中的二进制数据
 * - JSON 中的二进制数据
 * 
 * 示例：
 *   Base64::encode("Hello")  → "SGVsbG8="
 *   Base64::decode("SGVsbG8=")  → "Hello"
 */
std::string base64Encode(const std::string& data);

/**
 * @brief Base64 解码
 * @param encoded Base64 编码字符串
 * @return 解码后的原始数据
 */
std::string base64Decode(const std::string& encoded);

/**
 * @brief Base64 工具类
 */
class Base64 {
public:
    static std::string encode(const std::string& data);
    static std::string decode(const std::string& encoded);
    
    // 重载，支持任意二进制数据
    static std::string encode(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> decodeToBytes(const std::string& encoded);
};

// =====================================================
// 第四部分：UUID 生成
// =====================================================
/**
 * @brief 生成 UUID v4（随机 UUID）
 * @return UUID 字符串
 * 
 * UUID 格式：xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
 * 
 * 示例：
 *   "f47ac10b-58cc-4372-a567-0e02b2c3d479"
 */
std::string generateUUID();

// =====================================================
// 第五部分：时间处理
// =====================================================
/**
 * @brief 获取当前 Unix 时间戳（秒）
 */
time_t getCurrentTimestamp();

/**
 * @brief 获取当前 Unix 时间戳（毫秒）
 */
uint64_t getCurrentTimestampMs();

/**
 * @brief 时间戳转字符串
 * @param timestamp Unix 时间戳（秒）
 * @param format 时间格式（如 "%Y-%m-%d %H:%M:%S"）
 * @return 格式化的时间字符串
 * 
 * format 参数说明：
 *   %Y  四位年份（2024）
 *   %m  月份（01-12）
 *   %d  日期（01-31）
 *   %H  小时（00-23）
 *   %M  分钟（00-59）
 *   %S  秒（00-59）
 */
std::string formatTime(time_t timestamp, const std::string& format = "%Y-%m-%d %H:%M:%S");

/**
 * @brief 获取当前时间的格式化字符串
 */
std::string getCurrentTimeString(const std::string& format = "%Y-%m-%d %H:%M:%S");

// =====================================================
// 第六部分：其他工具
// =====================================================
/**
 * @brief URL 编码
 * @param str 输入字符串
 * @return URL 编码后的字符串
 * 
 * 将特殊字符转换为 %XX 格式
 * 
 * 示例：
 *   urlEncode("hello world")  → "hello%20world"
 *   urlEncode("a=1&b=2")      → "a%3D1%26b%3D2"
 */
std::string urlEncode(const std::string& str);

/**
 * @brief URL 解码
 */
std::string urlDecode(const std::string& str);

/**
 * @brief MD5 哈希
 * @param str 输入字符串
 * @return 32 位 MD5 哈希（小写十六进制）
 */
std::string md5(const std::string& str);

/**
 * @brief 判断文件是否存在
 */
bool fileExists(const std::string& path);

/**
 * @brief 读取文件内容
 */
std::string readFile(const std::string& path);

/**
 * @brief 写入文件内容
 */
bool writeFile(const std::string& path, const std::string& content);

/**
 * @brief 获取环境变量
 */
std::string getEnv(const std::string& key, const std::string& default_value = "");

#endif // UTILS_H
