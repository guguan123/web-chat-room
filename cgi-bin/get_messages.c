#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h> // 用于时间格式化
#include "cJSON.h" // 包含 cJSON 库的头文件

#define DB_PATH "/tmp/chat_messages.db" // 数据库文件路径
#define MAX_MESSAGES 50 // 限制获取的消息数量

// 辅助函数：将时间戳格式化为 RFC 3339 格式
void format_timestamp_rfc3339(time_t rawtime, char* buffer, size_t len) {
    struct tm *info;
    info = gmtime(&rawtime); // 使用 GMT（格林尼治标准时间）获取 UTC 时间
    strftime(buffer, len, "%Y-%m-%dT%H:%M:%SZ", info); // RFC 3339 格式字符串
}

int main() {
    sqlite3 *db; // SQLite 数据库连接对象
    sqlite3_stmt *stmt; // SQLite 预处理语句对象
    int rc; // SQLite 操作的返回码

    // 设置 CGI 响应头，指示内容类型为 JSON
    printf("Content-type: application/json\r\n\r\n");

    // 打开 SQLite 数据库连接
    rc = sqlite3_open(DB_PATH, &db);
    if (rc) {
        // 如果打开数据库失败，则输出错误信息到标准错误流，并返回错误码
        fprintf(stderr, "{\"error\": \"Can't open database: %s\"}\n", sqlite3_errmsg(db));
        return 1;
    }

    // 创建一个 cJSON 数组，用于存储所有消息对象
    cJSON *root = cJSON_CreateArray();
    if (root == NULL) {
        // 如果创建 JSON 数组失败，则输出错误信息，关闭数据库，并返回错误码
        fprintf(stderr, "{\"error\": \"Failed to create JSON array\"}\n");
        sqlite3_close(db);
        return 1;
    }

    // SQL 查询语句：选择最新的 MAX_MESSAGES 条消息，按 ID 降序排列 (ID 通常隐式地按时间戳生成)
    const char *sql = "SELECT id, timestamp, ip, username, message FROM messages ORDER BY id DESC LIMIT ?;";
    // 准备 SQL 语句
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        // 如果准备语句失败，则输出错误信息，释放 JSON 对象，关闭数据库，并返回错误码
        fprintf(stderr, "{\"error\": \"Failed to prepare statement: %s\"}\n", sqlite3_errmsg(db));
        cJSON_Delete(root);
        sqlite3_close(db);
        return 1;
    }

    // 绑定 MAX_MESSAGES 到 SQL 语句中的 LIMIT 参数
    sqlite3_bind_int(stmt, 1, MAX_MESSAGES);

    // 创建一个临时 cJSON 数组，用于按逆序（从最新到最旧）存储从数据库中获取的消息
    cJSON *temp_array = cJSON_CreateArray();
    if (temp_array == NULL) {
        // 如果创建临时 JSON 数组失败，则输出错误信息，释放 JSON 对象，结束语句，关闭数据库，并返回错误码
        fprintf(stderr, "{\"error\": \"Failed to create temporary JSON array\"}\n");
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
            // 如果创建消息 JSON 对象失败，则输出错误信息，释放所有 JSON 对象，结束语句，关闭数据库，并返回错误码
            fprintf(stderr, "{\"error\": \"Failed to create JSON object for message\"}\n");
            cJSON_Delete(root);
            cJSON_Delete(temp_array);
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            return 1;
        }

        // 从查询结果中获取消息的各个字段
        const char *id = (const char *)sqlite3_column_text(stmt, 0); // 消息 ID
        time_t timestamp_raw = (time_t)sqlite3_column_int64(stmt, 1); // 原始时间戳
        const char *ip = (const char *)sqlite3_column_text(stmt, 2); // 用户 IP 地址
        const char *username = (const char *)sqlite3_column_text(stmt, 3); // 用户名
        const char *message = (const char *)sqlite3_column_text(stmt, 4); // 消息内容

        char timestamp_str[30]; // 定义一个足够大的缓冲区来存储格式化后的时间戳
        format_timestamp_rfc3339(timestamp_raw, timestamp_str, sizeof(timestamp_str)); // 格式化时间戳

        // 将消息字段添加到 JSON 对象中
        cJSON_AddStringToObject(message_obj, "id", id);
        cJSON_AddStringToObject(message_obj, "timestamp", timestamp_str);
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
        // 如果 JSON 打印失败，则输出错误信息
        fprintf(stderr, "{\"error\": \"Failed to print JSON\"}\n");
    }

    cJSON_Delete(root); // 释放根 JSON 数组的内存

    return 0; // 程序成功执行
}
