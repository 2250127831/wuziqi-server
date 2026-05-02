/**
 * @file utils.cpp
 * @brief 工具函数实现
 */

#include "utils.h"
#include <algorithm>     // std::find_if, std::transform
#include <cctype>       // isspace, isdigit, isxdigit
#include <fstream>      // std::ifstream, std::ofstream
#include <sstream>      // std::stringstream
#include <random>       // std::random_device, std::mt19937
#include <chrono>       // std::chrono::system_clock
#include <cstdlib>      // std::getenv
#include <openssl/md5.h> // OpenSSL MD5（如果可用）
#include <iomanip>      // std::setw, std::setfill

// =====================================================
// 第一部分：字符串处理实现
// =====================================================

/**
 * @brief 去除首尾空白
 */
std::string trim(const std::string& str) {
    // 找第一个非空白字符的位置
    size_t first = 0;
    while (first < str.size() && std::isspace(str[first])) {
        ++first;
    }
    
    // 找最后一个非空白字符的位置
    size_t last = str.size();
    while (last > first && std::isspace(str[last - 1])) {
        --last;
    }
    
    return str.substr(first, last - first);
}

/**
 * @brief 去除左边空白
 */
std::string trimLeft(const std::string& str) {
    size_t first = 0;
    while (first < str.size() && std::isspace(str[first])) {
        ++first;
    }
    return str.substr(first);
}

/**
 * @brief 去除右边空白
 */
std::string trimRight(const std::string& str) {
    size_t last = str.size();
    while (last > 0 && std::isspace(str[last - 1])) {
        --last;
    }
    return str.substr(0, last);
}

/**
 * @brief 字符串分割
 * 
 * 原理：
 * 1. 找到分隔符的位置
 * 2. 截取子串
 * 3. 继续找下一个
 */
std::vector<std::string> split(const std::string& str, const std::string& delimiter) {
    std::vector<std::string> result;
    
    if (str.empty()) return result;
    
    size_t start = 0;
    size_t pos = 0;
    
    while ((pos = str.find(delimiter, start)) != std::string::npos) {
        // 找到一个分隔符，截取前面的部分
        result.push_back(str.substr(start, pos - start));
        start = pos + delimiter.size();
    }
    
    // 最后一个部分（分隔符之后的所有内容）
    result.push_back(str.substr(start));
    
    return result;
}

/**
 * @brief 字符串替换
 * 
 * 原理：
 * 1. 找到要替换的字符串
 * 2. 替换它
 * 3. 继续往后找
 */
std::string replace(const std::string& str, const std::string& from, const std::string& to) {
    if (from.empty()) return str;
    
    std::string result = str;
    size_t pos = 0;
    
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.size(), to);
        pos += to.size();  // 跳过已替换的部分，避免无限循环
    }
    
    return result;
}

/**
 * @brief 转大写
 */
std::string toUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

/**
 * @brief 转小写
 */
std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

/**
 * @brief 判断是否以指定前缀开始
 */
bool startsWith(const std::string& str, const std::string& prefix) {
    if (prefix.size() > str.size()) return false;
    return str.substr(0, prefix.size()) == prefix;
}

/**
 * @brief 判断是否以指定后缀结束
 */
bool endsWith(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.substr(str.size() - suffix.size()) == suffix;
}

// =====================================================
// 第二部分：JSON 实现
// =====================================================

// 内部使用：跳过空白字符
static void skipWhitespace(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(s[i])) ++i;
}

// 内部使用：解析字符串
static std::string parseString(const std::string& s, size_t& i) {
    // s[i] 应该是 '"'
    ++i;  // 跳过开始的 '"'
    
    std::string result;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) {
            // 转义字符
            ++i;
            switch (s[i]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"'; break;
                case 'u':
                    // Unicode 转义（简化处理）
                    result += 'u';
                    break;
                default:
                    result += s[i];
            }
        } else {
            result += s[i];
        }
        ++i;
    }
    
    // 跳过结束的 '"'
    if (i < s.size() && s[i] == '"') ++i;
    
    return result;
}

// 内部使用：解析数字
static double parseNumber(const std::string& s, size_t& i) {
    size_t start = i;
    
    // 处理负数
    if (s[i] == '-') ++i;
    
    // 整数部分
    while (i < s.size() && std::isdigit(s[i])) ++i;
    
    // 小数部分
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && std::isdigit(s[i])) ++i;
    }
    
    // 科学计数法
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        while (i < s.size() && std::isdigit(s[i])) ++i;
    }
    
    return std::stod(s.substr(start, i - start));
}

// 内部使用：解析 JSON 值
static JsonValue parseValue(const std::string& s, size_t& i);

/**
 * @brief 解析对象 { "key": value, ... }
 */
static JsonValue parseObject(const std::string& s, size_t& i) {
    JsonValue result;
    result.type = JsonType::OBJECT;
    
    // s[i] 应该是 '{'
    ++i;  // 跳过 '{'
    skipWhitespace(s, i);
    
    // 空对象
    if (i < s.size() && s[i] == '}') {
        ++i;
        return result;
    }
    
    while (true) {
        skipWhitespace(s, i);
        
        // 解析 key
        std::string key = parseString(s, i);
        
        skipWhitespace(s, i);
        
        // 跳过 ':'
        if (i < s.size() && s[i] == ':') ++i;
        
        // 解析 value
        JsonValue value = parseValue(s, i);
        
        result.obj[key] = value;
        
        skipWhitespace(s, i);
        
        // 检查是否还有更多
        if (s[i] == '}') {
            ++i;
            break;
        }
        
        // 跳过 ','
        if (s[i] == ',') ++i;
    }
    
    return result;
}

/**
 * @brief 解析数组 [ value, ... ]
 */
static JsonValue parseArray(const std::string& s, size_t& i) {
    JsonValue result;
    result.type = JsonType::ARRAY;
    
    // s[i] 应该是 '['
    ++i;  // 跳过 '['
    skipWhitespace(s, i);
    
    // 空数组
    if (i < s.size() && s[i] == ']') {
        ++i;
        return result;
    }
    
    while (true) {
        JsonValue value = parseValue(s, i);
        result.arr.push_back(value);
        
        skipWhitespace(s, i);
        
        // 检查是否还有更多
        if (s[i] == ']') {
            ++i;
            break;
        }
        
        // 跳过 ','
        if (s[i] == ',') ++i;
    }
    
    return result;
}

/**
 * @brief 解析 JSON 值
 */
static JsonValue parseValue(const std::string& s, size_t& i) {
    skipWhitespace(s, i);
    
    JsonValue result;
    char c = s[i];
    
    if (c == '"') {
        // 字符串
        result.type = JsonType::STRING;
        result.str_value = parseString(s, i);
    } else if (c == '{') {
        // 对象
        result = parseObject(s, i);
    } else if (c == '[') {
        // 数组
        result = parseArray(s, i);
    } else if (c == 't' || c == 'f') {
        // 布尔值
        result.type = JsonType::BOOLEAN;
        if (s.substr(i, 4) == "true") {
            result.bool_value = true;
            i += 4;
        } else if (s.substr(i, 5) == "false") {
            result.bool_value = false;
            i += 5;
        }
    } else if (c == 'n') {
        // null
        result.type = JsonType::NULL_TYPE;
        if (s.substr(i, 4) == "null") {
            i += 4;
        }
    } else {
        // 数字
        result.type = JsonType::NUMBER;
        result.num_value = parseNumber(s, i);
    }
    
    return result;
}

/**
 * @brief 解析 JSON 字符串
 */
JsonValue Json::parse(const std::string& json_str) {
    size_t i = 0;
    return parseValue(json_str, i);
}

/**
 * @brief JsonValue::operator[]
 */
const JsonValue& JsonValue::operator[](const std::string& key) const {
    static JsonValue empty;
    auto it = obj.find(key);
    if (it != obj.end()) {
        return it->second;
    }
    return empty;
}

/**
 * @brief JsonValue::operator[]
 */
const JsonValue& JsonValue::operator[](size_t index) const {
    static JsonValue empty;
    if (index < arr.size()) {
        return arr[index];
    }
    return empty;
}

/**
 * @brief 获取数组大小
 */
size_t JsonValue::size() const {
    if (type == JsonType::ARRAY) {
        return arr.size();
    } else if (type == JsonType::OBJECT) {
        return obj.size();
    }
    return 0;
}

// =====================================================
// 第三部分：Base64 实现
// =====================================================

// Base64 编码表
static const char* base64_chars = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

/**
 * @brief Base64 编码
 */
std::string base64Encode(const std::string& data) {
    std::string result;
    
    int i = 0;
    int j = 0;
    
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    
    for (char c : data) {
        char_array_3[i++] = c;
        
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for (int k = 0; k < 4; k++) {
                result += base64_chars[char_array_4[k]];
            }
            
            i = 0;
        }
    }
    
    if (i > 0) {
        for (int k = i; k < 3; k++) {
            char_array_3[k] = '\0';
        }
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        
        for (int k = 0; k < i + 1; k++) {
            result += base64_chars[char_array_4[k]];
        }
        
        // 添加填充字符
        while ((i++ < 3)) {
            result += '=';
        }
    }
    
    return result;
}

/**
 * @brief Base64 解码
 */
std::string base64Decode(const std::string& encoded) {
    std::string result;
    
    int i = 0;
    int j = 0;
    
    unsigned char char_array_4[4];
    unsigned char char_array_3[3];
    
    // 过滤非 Base64 字符
    std::string filtered;
    for (char c : encoded) {
        if (c != '=' && (base64_chars[0] == '\0' || strchr(base64_chars, c))) {
            filtered += c;
        }
    }
    
    for (char c : filtered) {
        char_array_4[i++] = c;
        
        if (i == 4) {
            char_array_4[0] = strchr(base64_chars, char_array_4[0]) - base64_chars;
            char_array_4[1] = strchr(base64_chars, char_array_4[1]) - base64_chars;
            char_array_4[2] = strchr(base64_chars, char_array_4[2]) - base64_chars;
            char_array_4[3] = strchr(base64_chars, char_array_4[3]) - base64_chars;
            
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0x0f) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + char_array_4[3];
            
            for (int k = 0; k < 3; k++) {
                result += char_array_3[k];
            }
            
            i = 0;
        }
    }
    
    return result;
}

std::string Base64::encode(const std::string& data) {
    return base64Encode(data);
}

std::string Base64::decode(const std::string& encoded) {
    return base64Decode(encoded);
}

// =====================================================
// 第四部分：UUID 生成
// =====================================================

/**
 * @brief 生成 UUID v4
 */
std::string generateUUID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);
    
    std::ostringstream oss;
    
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            oss << '-';
        } else if (i == 14) {
            oss << '4';  // UUID 版本 4
        } else if (i == 19) {
            oss << "0123456789abcdef"[dis2(gen)];  // 变体
        } else {
            oss << "0123456789abcdef"[dis(gen)];
        }
    }
    
    return oss.str();
}

// =====================================================
// 第五部分：时间处理
// =====================================================

/**
 * @brief 获取当前时间戳（秒）
 */
time_t getCurrentTimestamp() {
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

/**
 * @brief 获取当前时间戳（毫秒）
 */
uint64_t getCurrentTimestampMs() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

/**
 * @brief 格式化时间
 */
std::string formatTime(time_t timestamp, const std::string& format) {
    struct tm tm_result;
    localtime_r(&timestamp, &tm_result);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_result, format.c_str());
    return oss.str();
}

/**
 * @brief 获取当前时间字符串
 */
std::string getCurrentTimeString(const std::string& format) {
    return formatTime(getCurrentTimestamp(), format);
}

// =====================================================
// 第六部分：其他工具实现
// =====================================================

/**
 * @brief URL 编码
 */
std::string urlEncode(const std::string& str) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase;
    
    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else {
            oss << '%' << std::setw(2) << std::setfill('0') << (int)c;
        }
    }
    
    return oss.str();
}

/**
 * @brief URL 解码
 */
std::string urlDecode(const std::string& str) {
    std::ostringstream oss;
    
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int value;
            std::istringstream iss(str.substr(i + 1, 2));
            if (iss >> std::hex >> value) {
                oss << (char)value;
                i += 2;
            } else {
                oss << str[i];
            }
        } else if (str[i] == '+') {
            oss << ' ';
        } else {
            oss << str[i];
        }
    }
    
    return oss.str();
}

/**
 * @brief MD5 哈希
 */
std::string md5(const std::string& str) {
#ifdef USE_OPENSSL
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5((const unsigned char*)str.c_str(), str.size(), digest);
    
    std::ostringstream oss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    }
    return oss.str();
#else
    // 如果没有 OpenSSL，返回空字符串
    return "";
#endif
}

/**
 * @brief 判断文件是否存在
 */
bool fileExists(const std::string& path) {
    std::ifstream file(path);
    return file.good();
}

/**
 * @brief 读取文件
 */
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

/**
 * @brief 写入文件
 */
bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    file << content;
    return file.good();
}

/**
 * @brief 获取环境变量
 */
std::string getEnv(const std::string& key, const std::string& default_value) {
    const char* value = std::getenv(key.c_str());
    return value ? value : default_value;
}

// =====================================================
// JSON 辅助函数
// =====================================================

JsonValue Json::string(const std::string& value) {
    JsonValue v;
    v.type = JsonType::STRING;
    v.str_value = value;
    return v;
}

JsonValue Json::number(double value) {
    JsonValue v;
    v.type = JsonType::NUMBER;
    v.num_value = value;
    return v;
}

JsonValue Json::boolean(bool value) {
    JsonValue v;
    v.type = JsonType::BOOLEAN;
    v.bool_value = value;
    return v;
}

JsonValue Json::null() {
    JsonValue v;
    v.type = JsonType::NULL_TYPE;
    return v;
}

JsonValue Json::object() {
    JsonValue v;
    v.type = JsonType::OBJECT;
    return v;
}

JsonValue Json::array() {
    JsonValue v;
    v.type = JsonType::ARRAY;
    return v;
}
