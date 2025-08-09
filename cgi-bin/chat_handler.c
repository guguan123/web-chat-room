#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h> // 用于检查文件是否存在
#include "cJSON.h"

#define DB_PATH "/tmp/chat_messages.db"
#define MAX_MESSAGES_GET 50 // 用于GET请求限制获取的消息数量
#define MAX_MESSAGE_LENGTH 1024 // 消息内容的最大长度
#define MAX_MESSAGES_POST 200 // 数据库中保留的最大消息数量（用于POST请求清理旧消息）
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
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= '0' && a <= '9') a -= '0';
            else a -= 'A' - 10;
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

// 新增函数：解析 HTTP Cookie 字符串，获取用户名和密码
void parse_cookies(const char *cookie_str, char *username, size_t username_size, char *password, size_t password_size) {
    if (!cookie_str) return;

    char *cookie_copy = strdup(cookie_str);
    if (!cookie_copy) return;

    char *token;
    char *rest = cookie_copy;

    while ((token = strtok_r(rest, ";", &rest))) {
        // Trim leading spaces
        while (*token == ' ') token++;

        char *key = token;
        char *value = strchr(token, '=');

        if (value) {
            *value = '\0';
            value++;

            char decoded_value[MAX_MESSAGE_LENGTH + 1];
            url_decode(decoded_value, value);

            if (strcmp(key, "username") == 0) {
                strncpy(username, decoded_value, username_size - 1);
                username[username_size - 1] = '\0';
            } else if (strcmp(key, "password") == 0) {
                strncpy(password, decoded_value, password_size - 1);
                password[password_size - 1] = '\0';
            }
        }
    }
    free(cookie_copy);
}

// 函数：初始化数据库
int init_database() {
    sqlite3 *db;
    char *err_msg = 0;
    int rc;

    // 检查数据库文件是否存在
    struct stat buffer;
    if (stat(DB_PATH, &buffer) == 0) {
        // 文件存在，不需要创建
        // fprintf(stderr, "Database already exists at %s. Skipping creation.\n", DB_PATH);
        return 0;
    }

    fprintf(stderr, "Creating new database at %s...\n", DB_PATH);

    // 打开数据库连接（如果文件不存在，会自动创建）
    rc = sqlite3_open(DB_PATH, &db);
    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // 创建 messages 表的 SQL 语句
    const char *sql_create_table = "CREATE TABLE messages ("
                                   "id INTEGER PRIMARY KEY,"
                                   "timestamp INTEGER,"
                                   "ip TEXT,"
                                   "username TEXT,"
                                   "message TEXT"
                                   ");";
                                   
    // 创建 users 表的 SQL 语句，用于存储用户名和密码
    const char *sql_create_users_table = "CREATE TABLE users ("
                                         "username TEXT PRIMARY KEY,"
                                         "password TEXT"
                                         ");";

    // 执行 SQL 语句
    rc = sqlite3_exec(db, sql_create_table, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }

    // 执行创建 users 表的 SQL 语句
    rc = sqlite3_exec(db, sql_create_users_table, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return 1;
    }

    sqlite3_close(db);
    fprintf(stderr, "Database created successfully.\n");

    // 设置数据库文件权限
    if (chmod(DB_PATH, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) != 0) { // 660 权限
        fprintf(stderr, "Error setting database file permissions.\n");
        return 1;
    }

    return 0;
}


// 处理 GET 请求的函数 (原 get_messages.c 的核心逻辑)
int handle_get_messages() {
    sqlite3 *db; // SQLite 数据库连接对象
    sqlite3_stmt *stmt; // SQLite 预处理语句对象
    int rc; // SQLite 操作的返回码

    // 设置 CGI 响应头，指示内容类型为 JSON
    printf("Content-type: application/json\r\n\r\n");

    // 打开 SQLite 数据库连接
    rc = sqlite3_open(DB_PATH, &db);
    if (rc) {
        fprintf(stderr, "{\"error\": \"Can't open database: %s\"}\n", sqlite3_errmsg(db)); // 如果打开数据库失败，则输出错误信息到标准错误流，并返回错误码
        return 1;
    }

    // 创建一个 cJSON 数组，用于存储所有消息对象
    cJSON *root = cJSON_CreateArray();
    if (root == NULL) {
        fprintf(stderr, "{\"error\": \"Failed to create JSON array\"}\n"); // 如果创建 JSON 数组失败，则输出错误信息，关闭数据库，并返回错误码
        sqlite3_close(db);
        return 1;
    }

    // SQL 查询语句：选择最新的 MAX_MESSAGES_GET 条消息，按 ID 降序排列 (ID 通常隐式地按时间戳生成)
    const char *sql = "SELECT id, timestamp, ip, username, message FROM messages ORDER BY id DESC LIMIT ?;";
    // 准备 SQL 语句
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "{\"error\": \"Failed to prepare statement: %s\"}\n", sqlite3_errmsg(db)); // 如果准备语句失败，则输出错误信息，释放 JSON 对象，关闭数据库，并返回错误码
        cJSON_Delete(root);
        sqlite3_close(db);
        return 1;
    }

    // 绑定 MAX_MESSAGES_GET 到 SQL 语句中的 LIMIT 参数
    sqlite3_bind_int(stmt, 1, MAX_MESSAGES_GET);

    // 创建一个临时 cJSON 数组，用于按逆序（从最新到最旧）存储从数据库中获取的消息
    cJSON *temp_array = cJSON_CreateArray();
    if (temp_array == NULL) {
        fprintf(stderr, "{\"error\": \"Failed to create temporary JSON array\"}\n"); // 如果创建临时 JSON 数组失败，则输出错误信息，释放 JSON 对象，结束语句，关闭数据库，并返回错误码
        cJSON_Delete(root);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 1;
    }

    // 循环遍历查询结果集，每次获取一行消息数据
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        // 为当前消息创建一个 cJSON 对象
        cJSON *message_obj = cJSON_CreateObject();
        if (message_obj == NULL) {
            fprintf(stderr, "{\"error\": \"Failed to create JSON object for message\"}\n"); // 如果创建消息 JSON 对象失败，则输出错误信息，释放所有 JSON 对象，结束语句，关闭数据库，并返回错误码
            cJSON_Delete(root);
            cJSON_Delete(temp_array);
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            return 1;
        }

        // 从查询结果中获取消息的各个字段
        const char *id = (const char *)sqlite3_column_text(stmt, 0); // 消息 ID
        long long timestamp_raw = sqlite3_column_int64(stmt, 1); // 时间戳 (long long 类型以确保兼容性)
        const char *ip = (const char *)sqlite3_column_text(stmt, 2); // 用户 IP 地址
        const char *username = (const char *)sqlite3_column_text(stmt, 3); // 用户名
        const char *message = (const char *)sqlite3_column_text(stmt, 4); // 消息内容

        // 将消息字段添加到 JSON 对象中
        cJSON_AddStringToObject(message_obj, "id", id);
        cJSON_AddNumberToObject(message_obj, "timestamp", timestamp_raw); // 直接添加数字类型的时间戳
        cJSON_AddStringToObject(message_obj, "ip", ip);
        cJSON_AddStringToObject(message_obj, "username", username);
        cJSON_AddStringToObject(message_obj, "message", message);

        // 将当前消息 JSON 对象添加到临时数组中（此时是逆序）
        cJSON_AddItemToArray(temp_array, message_obj);
    }

    // 将消息从临时数组（逆序）添加到根数组（正序），实现按时间顺序排列
    for (int i = cJSON_GetArraySize(temp_array) - 1; i >= 0; i--) {
        cJSON_AddItemToArray(root, cJSON_DetachItemFromArray(temp_array, i));
    }
    cJSON_Delete(temp_array); // 释放临时数组的内存

    sqlite3_finalize(stmt); // 结束 SQLite 预处理语句
    sqlite3_close(db); // 关闭 SQLite 数据库连接

    // 将根 JSON 数组打印为未格式化的 JSON 字符串
    char *json_output = cJSON_PrintUnformatted(root);
    if (json_output != NULL) {
        printf("%s\n", json_output); // 输出 JSON 字符串到标准输出
        free(json_output); // 释放 JSON 字符串内存
    } else {
        fprintf(stderr, "{\"error\": \"Failed to print JSON\"}\n"); // 如果 JSON 打印失败，则输出错误信息
    }

    cJSON_Delete(root); // 释放根 JSON 数组的内存

    return 0; // 程序成功执行
}

// 处理 POST 请求的函数 (原 post_message.c 的核心逻辑)
int handle_post_message() {
    // 设置 CGI 响应头，指示内容类型为纯文本
    printf("Content-type: text/plain\r\n\r\n");

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
    char password[256] = ""; // 密码缓冲区
    char message[MAX_MESSAGE_LENGTH + 1] = ""; // 消息内容缓冲区
    char decoded_value[MAX_MESSAGE_LENGTH + 1]; // 用于存储解码后的值

    char *token; // 用于 strtok_r 的令牌
    char *rest = post_data; // 用于 strtok_r 的剩余字符串指针

    // 解析 POST 数据，只获取消息内容
    while ((token = strtok_r(rest, "&", &rest))) { // 按 '&' 分割键值对
        char *key = token;
        char *value = strchr(token, '='); // 查找 '=' 分隔符
        if (value) {
            *value = '\0'; // 在 '=' 处截断，将 key 字符串空终止
            value++;       // 移动指针到值的开始
            url_decode(decoded_value, value); // 解码值
            if (strcmp(key, "message") == 0) {
                strncpy(message, decoded_value, sizeof(message) - 1); // 复制解码后的消息
                message[sizeof(message) - 1] = '\0'; // 确保空终止
            }
        }
    }
    
    // 从环境变量中获取 Cookie，并解析用户名和密码
    const char *cookie_str = getenv("HTTP_COOKIE");
    parse_cookies(cookie_str, username, sizeof(username), password, sizeof(password));

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
    
    // 如果没有从Cookie中获取到用户名，则使用默认值
    if (strlen(username) == 0) {
        strncpy(username, "anonymous", sizeof(username));
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

    // ========== 身份验证逻辑开始 ==========
    if (strcmp(username, "anonymous") != 0) {
        // 尝试从 users 表中查询用户
        const char *sql_check_user = "SELECT password FROM users WHERE username = ?;";
        rc = sqlite3_prepare_v2(db, sql_check_user, -1, &stmt, 0);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Error: Failed to prepare user check statement: %s\n", sqlite3_errmsg(db));
            sqlite3_close(db);
            return 1;
        }
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
        
        // 检查用户是否存在
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            // 用户已存在，检查密码是否匹配
            const char *stored_password = (const char *)sqlite3_column_text(stmt, 0);
            if (strlen(password) == 0 || strcmp(password, stored_password) != 0) {
                sqlite3_finalize(stmt);
                sqlite3_close(db);
                printf("Error: Incorrect password for user '%s'.\n", username);
                return 1;
            }
        } else if (rc == SQLITE_DONE) {
            // 用户不存在，如果是新注册则插入
            if (strlen(password) > 0) {
                sqlite3_finalize(stmt); // 结束查询语句
                const char *sql_insert_user = "INSERT INTO users (username, password) VALUES (?, ?);";
                rc = sqlite3_prepare_v2(db, sql_insert_user, -1, &stmt, 0);
                if (rc != SQLITE_OK) {
                    fprintf(stderr, "Error: Failed to prepare user insert statement: %s\n", sqlite3_errmsg(db));
                    sqlite3_close(db);
                    return 1;
                }
                sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);
                rc = sqlite3_step(stmt);
                if (rc != SQLITE_DONE) {
                    fprintf(stderr, "Error: Failed to execute user insert statement: %s\n", sqlite3_errmsg(db));
                    sqlite3_finalize(stmt);
                    sqlite3_close(db);
                    return 1;
                }
            }
        } else {
            // 查询出错
            fprintf(stderr, "Error: User check failed: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            return 1;
        }
        sqlite3_finalize(stmt); // 结束语句
    }
    // ========== 身份验证逻辑结束 ==========

    // 获取用户 IP 地址（优先从 Cloudflare 代理头获取，其次从 REMOTE_ADDR 获取）
    const char *user_ip = getenv("HTTP_CF_CONNECTING_IP");
    if (user_ip == NULL) {
        user_ip = getenv("REMOTE_ADDR");
    }
    if (user_ip == NULL || strlen(user_ip) == 0) {
        user_ip = "UNKNOWN_IP"; // 如果无法获取 IP，则设置为 "UNKNOWN_IP"
    }

    // SQL 插入语句：将新消息插入到 messages 表中
    const char *sql_insert = "INSERT INTO messages (timestamp, ip, username, message) VALUES (?, ?, ?, ?);";
    // 准备 SQL 插入语句
    rc = sqlite3_prepare_v2(db, sql_insert, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to prepare insert statement: %s\n", sqlite3_errmsg(db)); // 如果准备失败，则打印错误信息
        sqlite3_close(db); // 关闭数据库
        return 1; // 返回错误码
    }

    // 绑定参数到插入语句
    sqlite3_bind_int64(stmt, 1, time(NULL)); // 绑定时间戳
    sqlite3_bind_text(stmt, 2, user_ip, -1, SQLITE_STATIC); // 绑定用户 IP
    sqlite3_bind_text(stmt, 3, username, -1, SQLITE_STATIC); // 绑定用户名
    sqlite3_bind_text(stmt, 4, message, -1, SQLITE_STATIC); // 绑定消息内容

    // 执行插入语句
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error: Failed to execute insert statement: %s\n", sqlite3_errmsg(db)); // 如果执行失败，则打印错误信息
        sqlite3_finalize(stmt); // 结束语句
        sqlite3_close(db); // 关闭数据库
        return 1; // 返回错误码
    }
    sqlite3_finalize(stmt); // 结束语句

    // 清理旧消息：只保留最新的 MAX_MESSAGES_POST 条消息
    const char *sql_delete_old = "DELETE FROM messages WHERE id NOT IN (SELECT id FROM messages ORDER BY timestamp DESC, id DESC LIMIT ?);"; // 按时间戳和 ID 降序排序，然后限制数量
    // 准备 SQL 删除语句
    rc = sqlite3_prepare_v2(db, sql_delete_old, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to prepare delete statement: %s\n", sqlite3_errmsg(db)); // 如果准备失败，则打印错误信息
        sqlite3_close(db); // 关闭数据库
        return 1; // 返回错误码
    }
    sqlite3_bind_int(stmt, 1, MAX_MESSAGES_POST); // 绑定要保留的消息数量

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

int main() {
    // 在处理请求之前，先初始化数据库
    if (init_database() != 0) {
        printf("Content-type: text/plain\r\n\r\n");
        printf("Error: Failed to initialize database.\n");
        return 1;
    }

    char *request_method = getenv("REQUEST_METHOD"); // 获取请求方法

    if (request_method == NULL) {
        printf("Content-type: text/plain\r\n\r\n");
        printf("Error: REQUEST_METHOD not set.\n");
        return 1;
    }

    if (strcmp(request_method, "GET") == 0) {
        return handle_get_messages();
    } else if (strcmp(request_method, "POST") == 0) {
        return handle_post_message();
    } else {
        printf("Content-type: text/plain\r\n\r\n");
        printf("Error: Unsupported request method: %s\n", request_method);
        return 1;
    }
}
