#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <ctype.h>

#define DB_PATH "/tmp/chat_messages.db" // 数据库文件路径
#define MAX_MESSAGE_LENGTH 1024 // 消息内容的最大长度
#define MAX_MESSAGES 200 // 数据库中保留的最大消息数量
#define MAX_POST_DATA_SIZE 4096 // POST 数据缓冲区最大尺寸

// 函数：URL 解码字符串
void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        // 处理 %xx 形式的编码字符
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            // 将十六进制字符转换为对应的数值
            if (a >= 'a') a -= 'a' - 'A'; // 小写转大写
            if (a >= '0' && a <= '9') a -= '0'; // 数字字符转数字
            else a -= 'A' - 10; // 字母字符转数字 (A=10, B=11...)
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= '0' && b <= '9') b -= '0';
            else b -= 'A' - 10;
            *dst++ = 16*a + b; // 将两个半字节合并为一个字节
            src+=3; // 跳过 % 和两个十六进制字符
        } else if (*src == '+') {
            // 处理 '+' 编码，将其替换为空格
            *dst++ = ' ';
            src++;
        } else {
            // 其他字符直接复制
            *dst++ = *src++;
        }
    }
    *dst++ = '\0'; // 字符串以空字符结尾
}

int main() {
    // 设置 CGI 响应头，指示内容类型为纯文本
    printf("Content-type: text/plain\r\n\r\n");

    // 获取请求方法
    char *request_method = getenv("REQUEST_METHOD");
    // 检查请求方法是否为 POST
    if (request_method == NULL || strcmp(request_method, "POST") != 0) {
        printf("Error: This script only supports POST requests.\n"); // 如果不是 POST 请求，则打印错误信息
        return 1; // 返回错误码
    }

    // 获取 POST 请求的内容长度
    char *content_length_str = getenv("CONTENT_LENGTH");
    int content_length = 0;
    if (content_length_str != NULL) {
        content_length = atoi(content_length_str); // 将字符串转换为整数
    }

    // 检查内容长度是否有效
    if (content_length <= 0 || content_length > MAX_POST_DATA_SIZE) {
        printf("Error: Invalid or missing POST data length.\n"); // 如果内容长度无效，则打印错误信息
        return 1; // 返回错误码
    }

    // 读取 POST 数据到缓冲区
    char post_data[MAX_POST_DATA_SIZE + 1]; // +1 用于存储空字符
    if (fread(post_data, 1, content_length, stdin) != content_length) {
        printf("Error: Failed to read POST data from stdin.\n"); // 如果读取失败，则打印错误信息
        return 1; // 返回错误码
    }
    post_data[content_length] = '\0'; // 确保字符串以空字符结尾

    char username[256] = ""; // 用户名缓冲区
    char message[MAX_MESSAGE_LENGTH + 1] = ""; // 消息内容缓冲区
    char decoded_value[MAX_MESSAGE_LENGTH + 1]; // 用于存储解码后的值

    char *token; // 用于 strtok_r 的令牌
    char *rest = post_data; // 用于 strtok_r 的剩余字符串指针

    // 解析 URL 编码的 POST 数据（格式为 key1=value1&key2=value2...）
    while ((token = strtok_r(rest, "&", &rest))) { // 按 '&' 分割键值对
        char *key = token;
        char *value = strchr(token, '='); // 查找 '=' 分隔符
        if (value) {
            *value = '\0'; // 在 '=' 处截断，将 key 字符串空终止
            value++;       // 移动指针到值的开始
            url_decode(decoded_value, value); // 解码值

            // 根据键名判断是用户名还是消息
            if (strcmp(key, "username") == 0) {
                strncpy(username, decoded_value, sizeof(username) - 1); // 复制解码后的用户名
                username[sizeof(username) - 1] = '\0'; // 确保空终止
            } else if (strcmp(key, "message") == 0) {
                strncpy(message, decoded_value, sizeof(message) - 1); // 复制解码后的消息
                message[sizeof(message) - 1] = '\0'; // 确保空终止
            }
        }
    }

    // 检查消息内容是否为空
    if (strlen(message) == 0) {
        printf("Error: Message is empty.\n"); // 如果消息为空，则打印错误信息
        return 1; // 返回错误码
    }

    // 检查消息内容是否超出最大长度，如果超出则截断
    if (strlen(message) > MAX_MESSAGE_LENGTH) {
        message[MAX_MESSAGE_LENGTH] = '\0'; // 截断消息
        printf("Warning: Message truncated to %d characters.\n", MAX_MESSAGE_LENGTH); // 打印警告信息
    }

    sqlite3 *db; // SQLite 数据库连接对象
    sqlite3_stmt *stmt; // SQLite 预处理语句对象
    int rc; // SQLite 操作的返回码

    // 打开 SQLite 数据库连接
    rc = sqlite3_open(DB_PATH, &db);
    if (rc) {
        fprintf(stderr, "Error: Can't open database: %s\n", sqlite3_errmsg(db)); // 如果打开数据库失败，则打印错误信息
        return 1; // 返回错误码
    }

    // 获取用户 IP 地址（优先从 Cloudflare 代理头获取，其次从 REMOTE_ADDR 获取）
    const char *user_ip = getenv("HTTP_CF_CONNECTING_IP");
    if (user_ip == NULL) {
        user_ip = getenv("REMOTE_ADDR");
    }
    if (user_ip == NULL || strlen(user_ip) == 0) {
        user_ip = "UNKNOWN_IP"; // 如果无法获取 IP，则设置为 "UNKNOWN_IP"
    }

    // 生成基于时间戳的唯一消息 ID (使用秒级时间戳)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts); // 获取实时时间
    long long current_time_sec = (long long)ts.tv_sec; // 转换为秒级时间戳

    char message_id[64]; // 消息 ID 缓冲区
    snprintf(message_id, sizeof(message_id), "msg_%lld", current_time_sec); // 格式化消息 ID

    // SQL 插入语句：将新消息插入到 messages 表中
    const char *sql_insert = "INSERT INTO messages (id, timestamp, ip, username, message) VALUES (?, ?, ?, ?, ?);";
    // 准备 SQL 插入语句
    rc = sqlite3_prepare_v2(db, sql_insert, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to prepare insert statement: %s\n", sqlite3_errmsg(db)); // 如果准备失败，则打印错误信息
        sqlite3_close(db); // 关闭数据库
        return 1; // 返回错误码
    }

    // 绑定参数到插入语句
    sqlite3_bind_text(stmt, 1, message_id, -1, SQLITE_STATIC); // 绑定消息 ID
    sqlite3_bind_int64(stmt, 2, current_time_sec); // 绑定 Unix epoch 秒级时间戳
    sqlite3_bind_text(stmt, 3, user_ip, -1, SQLITE_STATIC); // 绑定用户 IP
    sqlite3_bind_text(stmt, 4, username, -1, SQLITE_STATIC); // 绑定用户名
    sqlite3_bind_text(stmt, 5, message, -1, SQLITE_STATIC); // 绑定消息内容

    // 执行插入语句
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error: Failed to execute insert statement: %s\n", sqlite3_errmsg(db)); // 如果执行失败，则打印错误信息
        sqlite3_finalize(stmt); // 结束语句
        sqlite3_close(db); // 关闭数据库
        return 1; // 返回错误码
    }
    sqlite3_finalize(stmt); // 结束语句

    // 清理旧消息：只保留最新的 MAX_MESSAGES 条消息
    const char *sql_delete_old = "DELETE FROM messages WHERE id NOT IN (SELECT id FROM messages ORDER BY timestamp DESC, id DESC LIMIT ?);"; // 按时间戳和 ID 降序排序，然后限制数量
    // 准备 SQL 删除语句
    rc = sqlite3_prepare_v2(db, sql_delete_old, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to prepare delete statement: %s\n", sqlite3_errmsg(db)); // 如果准备失败，则打印错误信息
        sqlite3_close(db); // 关闭数据库
        return 1; // 返回错误码
    }
    sqlite3_bind_int(stmt, 1, MAX_MESSAGES); // 绑定要保留的消息数量

    // 执行删除语句
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error: Failed to execute delete statement: %s\n", sqlite3_errmsg(db)); // 如果执行失败，则打印错误信息
        sqlite3_finalize(stmt); // 结束语句
        sqlite3_close(db); // 关闭数据库
        return 1; // 返回错误码
    }
    sqlite3_finalize(stmt); // 结束语句

    sqlite3_close(db); // 关闭数据库连接

    printf("OK: Message posted and old messages cleaned.\n"); // 打印成功信息

    return 0; // 程序成功执行
}
