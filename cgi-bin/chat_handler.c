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

// 函数：解析 HTTP Cookie 字符串，获取用户名和密码
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

// 函数：发送统一的 JSON 响应
void send_json_response(int http_status, const char *status_text, cJSON *json_body) {
	printf("Status: %d %s\r\n", http_status, status_text);
	printf("Content-type: application/json\r\n\r\n");
	char *json_output = cJSON_PrintUnformatted(json_body);
	if (json_output != NULL) {
		printf("%s\n", json_output);
		free(json_output);
	}
	cJSON_Delete(json_body);
}

// 函数：初始化数据库
int init_database() {
	sqlite3 *db;
	char *err_msg = 0;
	int rc;

	// 检查数据库文件是否存在
	struct stat buffer;
	if (stat(DB_PATH, &buffer) == 0) {
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


// 处理 GET 请求的函数
int handle_get_messages() {
	sqlite3 *db; // SQLite 数据库连接对象
	sqlite3_stmt *stmt; // SQLite 预处理语句对象
	int rc; // SQLite 操作的返回码

	// 打开 SQLite 数据库连接
	rc = sqlite3_open(DB_PATH, &db);
	if (rc) {
		// 如果打开数据库失败，则输出错误信息到标准错误流，并返回错误码
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", sqlite3_errmsg(db));
		send_json_response(500, "Internal Server Error", response_json);
		return 1;
	}

	cJSON *root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "status", "success");
	cJSON *data_array = cJSON_CreateArray();
	cJSON_AddItemToObject(root, "data", data_array);

	if (root == NULL || data_array == NULL) {
		// 如果创建 JSON 数组失败，则输出错误信息，关闭数据库，并返回错误码
		cJSON_Delete(root);
		sqlite3_close(db);
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Failed to create JSON array");
		send_json_response(500, "Internal Server Error", response_json);
		return 1;
	}

	// SQL 查询语句：选择最新的 MAX_MESSAGES_GET 条消息，按 ID 降序排列 (ID 通常隐式地按时间戳生成)
	const char *sql = "SELECT id, timestamp, ip, username, message FROM messages ORDER BY id DESC LIMIT ?;";
	// 准备 SQL 语句
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		// 如果准备语句失败，则输出错误信息，释放 JSON 对象，关闭数据库，并返回错误码
		cJSON_Delete(root);
		sqlite3_close(db);
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Failed to prepare statement");
		send_json_response(500, "Internal Server Error", response_json);
		return 1;
	}

	// 绑定 MAX_MESSAGES_GET 到 SQL 语句中的 LIMIT 参数
	sqlite3_bind_int(stmt, 1, MAX_MESSAGES_GET);

	// 创建一个临时 cJSON 数组，用于按逆序（从最新到最旧）存储从数据库中获取的消息
	cJSON *temp_array = cJSON_CreateArray();
	if (temp_array == NULL) {
		// 如果创建临时 JSON 数组失败，则输出错误信息，释放 JSON 对象，结束语句，关闭数据库，并返回错误码
		cJSON_Delete(root);
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Failed to create temporary JSON array");
		send_json_response(500, "Internal Server Error", response_json);
		return 1;
	}

	// 循环遍历查询结果集，每次获取一行消息数据
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		// 为当前消息创建一个 cJSON 对象
		cJSON *message_obj = cJSON_CreateObject();
		if (message_obj == NULL) {
			// 如果创建消息 JSON 对象失败，则输出错误信息，释放所有 JSON 对象，结束语句，关闭数据库，并返回错误码
			cJSON_Delete(root);
			cJSON_Delete(temp_array);
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			cJSON *response_json = cJSON_CreateObject();
			cJSON_AddStringToObject(response_json, "status", "error");
			cJSON_AddStringToObject(response_json, "message", "Failed to create JSON object for message");
			send_json_response(500, "Internal Server Error", response_json);
			return 1;
		}

		// 从查询结果中获取消息的各个字段
		const long long id_raw = sqlite3_column_int64(stmt, 0); // 消息 ID
		long long timestamp_raw = sqlite3_column_int64(stmt, 1); // 时间戳 (long long 类型以确保兼容性)
		const char *ip = (const char *)sqlite3_column_text(stmt, 2); // 用户 IP 地址
		const char *username = (const char *)sqlite3_column_text(stmt, 3); // 用户名
		const char *message = (const char *)sqlite3_column_text(stmt, 4); // 消息内容

		char id_str[20];
		snprintf(id_str, sizeof(id_str), "%lld", id_raw);

		// 将消息字段添加到 JSON 对象中
		cJSON_AddStringToObject(message_obj, "id", id_str);
		cJSON_AddNumberToObject(message_obj, "timestamp", timestamp_raw);
		cJSON_AddStringToObject(message_obj, "ip", ip);
		cJSON_AddStringToObject(message_obj, "username", username);
		cJSON_AddStringToObject(message_obj, "message", message);

		// 将当前消息 JSON 对象添加到临时数组中（此时是逆序）
		cJSON_AddItemToArray(temp_array, message_obj);
	}

	// 将消息从临时数组（逆序）添加到根数组（正序），实现按时间顺序排列
	for (int i = cJSON_GetArraySize(temp_array) - 1; i >= 0; i--) {
		cJSON_AddItemToArray(data_array, cJSON_DetachItemFromArray(temp_array, i));
	}
	cJSON_Delete(temp_array); // 释放临时数组的内存

	sqlite3_finalize(stmt); // 结束 SQLite 预处理语句
	sqlite3_close(db); // 关闭 SQLite 数据库连接

	send_json_response(200, "OK", root); // 返回响应并释放根 JSON 数组的内存

	return 0; // 程序成功执行
}

// 处理 POST 请求的函数（原先的聊天消息处理）
int handle_post_message() {
	// 获取 POST 请求的内容长度
	char *content_length_str = getenv("CONTENT_LENGTH");
	int content_length = 0;
	if (content_length_str != NULL) {
		content_length = atoi(content_length_str); // 将字符串转换为整数
	}

	// 检查内容长度是否有效
	if (content_length <= 0 || content_length > MAX_POST_DATA_SIZE) {
		// 如果内容长度无效，则打印错误信息
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Invalid or missing POST data length.");
		send_json_response(400, "Bad Request", response_json);
		return 1;
	}

	char post_data[MAX_POST_DATA_SIZE + 1];
	if (fread(post_data, 1, content_length, stdin) != content_length) {
		 // 如果读取失败，则打印错误信息
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Failed to read POST data from stdin.");
		send_json_response(500, "Internal Server Error", response_json);
		return 1;
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
		// 如果消息为空，则打印错误信息
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Message is empty.");
		send_json_response(400, "Bad Request", response_json);
		return 1;
	}

	// 检查消息内容是否超出最大长度，如果超出则截断
	if (strlen(message) > MAX_MESSAGE_LENGTH) {
		message[MAX_MESSAGE_LENGTH] = '\0'; // 截断消息
		fprintf(stderr, "Warning: Message truncated to %d characters.\n", MAX_MESSAGE_LENGTH); // 打印警告信息
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
		// 如果打开数据库失败，则打印错误信息
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Can't open database.");
		send_json_response(500, "Internal Server Error", response_json);
		return 1;
	}

	// ========== 身份验证逻辑开始 ==========
	if (strcmp(username, "anonymous") != 0) {
		// 尝试从 users 表中查询用户
		const char *sql_check_user = "SELECT password FROM users WHERE username = ?;";
		rc = sqlite3_prepare_v2(db, sql_check_user, -1, &stmt, 0);
		if (rc != SQLITE_OK) {
			sqlite3_close(db);
			cJSON *response_json = cJSON_CreateObject();
			cJSON_AddStringToObject(response_json, "status", "error");
			cJSON_AddStringToObject(response_json, "message", "Failed to prepare user check statement.");
			send_json_response(500, "Internal Server Error", response_json);
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
				cJSON *response_json = cJSON_CreateObject();
				cJSON_AddStringToObject(response_json, "status", "error");
				cJSON_AddStringToObject(response_json, "message", "Incorrect password or password not provided for existing user.");
				send_json_response(400, "Bad Request", response_json);
				return 1;
			}
		} else {
			// 查询出错
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			cJSON *response_json = cJSON_CreateObject();
			cJSON_AddStringToObject(response_json, "status", "error");
			cJSON_AddStringToObject(response_json, "message", "User check failed.");
			send_json_response(500, "Internal Server Error", response_json);
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
		// 如果准备失败，则打印错误信息
		sqlite3_close(db); // 关闭数据库
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Failed to prepare insert statement.");
		send_json_response(500, "Internal Server Error", response_json);
		return 1;
	}

	// 绑定参数到插入语句
	sqlite3_bind_int64(stmt, 1, time(NULL)); // 绑定时间戳
	sqlite3_bind_text(stmt, 2, user_ip, -1, SQLITE_STATIC); // 绑定用户 IP
	sqlite3_bind_text(stmt, 3, username, -1, SQLITE_STATIC); // 绑定用户名
	sqlite3_bind_text(stmt, 4, message, -1, SQLITE_STATIC); // 绑定消息内容

	// 执行插入语句
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		// 如果执行失败，则打印错误信息
		sqlite3_finalize(stmt); // 结束语句
		sqlite3_close(db); // 关闭数据库
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Failed to execute insert statement.");
		send_json_response(500, "Internal Server Error", response_json);
		return 1;
	}
	sqlite3_finalize(stmt); // 结束语句

	// 清理旧消息：只保留最新的 MAX_MESSAGES_POST 条消息
	const char *sql_delete_old = "DELETE FROM messages WHERE id NOT IN (SELECT id FROM messages ORDER BY timestamp DESC, id DESC LIMIT ?);"; // 按时间戳和 ID 降序排序，然后限制数量
	// 准备 SQL 删除语句
	rc = sqlite3_prepare_v2(db, sql_delete_old, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		// 如果准备失败，则打印错误信息
		sqlite3_close(db); // 关闭数据库
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Failed to prepare delete statement.");
		send_json_response(500, "Internal Server Error", response_json);
		return 1;
	}
	sqlite3_bind_int(stmt, 1, MAX_MESSAGES_POST); // 绑定要保留的消息数量

	// 执行删除语句
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		// 如果执行失败，则打印错误信息
		sqlite3_finalize(stmt); // 结束语句
		sqlite3_close(db); // 关闭数据库
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Failed to execute delete statement.");
		send_json_response(500, "Internal Server Error", response_json);
		return 1;
	}
	sqlite3_finalize(stmt); // 结束语句

	sqlite3_close(db); // 关闭数据库连接

	// 打印成功信息
	cJSON *response_json = cJSON_CreateObject();
	cJSON_AddStringToObject(response_json, "status", "success");
	cJSON_AddStringToObject(response_json, "message", "Message posted and old messages cleaned.");
	send_json_response(200, "OK", response_json);

	return 0; // 程序成功执行
}

// 新增：处理用户管理请求
int handle_user_management(const char *action, const char *request_method) {
	sqlite3 *db;
	sqlite3_stmt *stmt;
	int rc;

	rc = sqlite3_open(DB_PATH, &db);
	if (rc) {
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Can't open database for user management.");
		send_json_response(500, "Internal Server Error", response_json);
		return 1;
	}
	
	// 获取 POST/PATCH/DELETE 数据
	char post_data[MAX_POST_DATA_SIZE + 1] = "";
	char *content_length_str = getenv("CONTENT_LENGTH");
	int content_length = 0;
	if (content_length_str != NULL) {
		content_length = atoi(content_length_str);
	}
	if (content_length > 0 && content_length <= MAX_POST_DATA_SIZE) {
		fread(post_data, 1, content_length, stdin);
		post_data[content_length] = '\0';
	}

	char username[256] = "";
	char password[256] = "";
	char new_password[256] = "";

	// 解析表单数据
	char *token;
	char *rest = post_data;
	char decoded_value[MAX_MESSAGE_LENGTH + 1];
	while ((token = strtok_r(rest, "&", &rest))) {
		char *key = token;
		char *value = strchr(token, '=');
		if (value) {
			*value = '\0';
			value++;
			url_decode(decoded_value, value);
			if (strcmp(key, "username") == 0) {
				snprintf(username, sizeof(username), "%s", decoded_value);
			} else if (strcmp(key, "password") == 0) {
				snprintf(password, sizeof(password), "%s", decoded_value);
			} else if (strcmp(key, "new_password") == 0) {
				snprintf(new_password, sizeof(new_password), "%s", decoded_value);
			}
		}
	}
	
	// 如果是 DELETE 请求，则从 Cookie 中获取用户名和密码
	if (strcmp(request_method, "DELETE") == 0) {
		const char *cookie_str = getenv("HTTP_COOKIE");
		parse_cookies(cookie_str, username, sizeof(username), password, sizeof(password));
	}


	// 注册 (POST action=register)
	if (strcmp(request_method, "POST") == 0 && strcmp(action, "register") == 0) {
		if (strlen(username) == 0 || strlen(password) == 0) {
			cJSON *response_json = cJSON_CreateObject();
			cJSON_AddStringToObject(response_json, "status", "error");
			cJSON_AddStringToObject(response_json, "message", "Username and password are required.");
			send_json_response(400, "Bad Request", response_json);
			return 1;
		}

		const char *sql_check_user = "SELECT username FROM users WHERE username = ?;";
		rc = sqlite3_prepare_v2(db, sql_check_user, -1, &stmt, 0);
		sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			cJSON *response_json = cJSON_CreateObject();
			cJSON_AddStringToObject(response_json, "status", "error");
			cJSON_AddStringToObject(response_json, "message", "User already exists.");
			send_json_response(409, "Conflict", response_json);
			return 1;
		}
		sqlite3_finalize(stmt);
		
		const char *sql_insert_user = "INSERT INTO users (username, password) VALUES (?, ?);";
		rc = sqlite3_prepare_v2(db, sql_insert_user, -1, &stmt, 0);
		sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);
		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			cJSON *response_json = cJSON_CreateObject();
			cJSON_AddStringToObject(response_json, "status", "error");
			cJSON_AddStringToObject(response_json, "message", "Failed to register user.");
			send_json_response(500, "Internal Server Error", response_json);
			return 1;
		}
		sqlite3_finalize(stmt);

		sqlite3_close(db);
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "success");
		cJSON_AddStringToObject(response_json, "message", "User registered successfully.");
		send_json_response(200, "OK", response_json);
		return 0;
	}

	// 登录 (POST action=login)
	if (strcmp(request_method, "POST") == 0 && strcmp(action, "login") == 0) {
		if (strlen(username) == 0 || strlen(password) == 0) {
			cJSON *response_json = cJSON_CreateObject();
			cJSON_AddStringToObject(response_json, "status", "error");
			cJSON_AddStringToObject(response_json, "message", "Username and password are required.");
			send_json_response(400, "Bad Request", response_json);
			return 1;
		}
		const char *sql_check_user = "SELECT password FROM users WHERE username = ?;";
		rc = sqlite3_prepare_v2(db, sql_check_user, -1, &stmt, 0);
		sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *stored_password = (const char *)sqlite3_column_text(stmt, 0);
			if (strcmp(password, stored_password) == 0) {
				sqlite3_finalize(stmt);
				sqlite3_close(db);
				cJSON *response_json = cJSON_CreateObject();
				cJSON_AddStringToObject(response_json, "status", "success");
				cJSON_AddStringToObject(response_json, "message", "Login successful.");
				send_json_response(200, "OK", response_json);
				return 0;
			}
		}
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Invalid username or password.");
		send_json_response(401, "Unauthorized", response_json);
		return 1;
	}
	
	// 修改密码 (POST action=update)
	if (strcmp(request_method, "POST") == 0 && strcmp(action, "update") == 0) {
		if (strlen(username) == 0 || strlen(password) == 0 || strlen(new_password) == 0) {
			cJSON *response_json = cJSON_CreateObject();
			cJSON_AddStringToObject(response_json, "status", "error");
			cJSON_AddStringToObject(response_json, "message", "Username, old password, and new password are required.");
			send_json_response(400, "Bad Request", response_json);
			return 1;
		}

		const char *sql_check_user = "SELECT password FROM users WHERE username = ?;";
		rc = sqlite3_prepare_v2(db, sql_check_user, -1, &stmt, 0);
		sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *stored_password = (const char *)sqlite3_column_text(stmt, 0);
			if (strcmp(password, stored_password) == 0) {
				sqlite3_finalize(stmt);
				
				const char *sql_update = "UPDATE users SET password = ? WHERE username = ?;";
				rc = sqlite3_prepare_v2(db, sql_update, -1, &stmt, 0);
				sqlite3_bind_text(stmt, 1, new_password, -1, SQLITE_STATIC);
				sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
				if (sqlite3_step(stmt) == SQLITE_DONE) {
					sqlite3_finalize(stmt);
					sqlite3_close(db);
					cJSON *response_json = cJSON_CreateObject();
					cJSON_AddStringToObject(response_json, "status", "success");
					cJSON_AddStringToObject(response_json, "message", "Password updated successfully.");
					send_json_response(200, "OK", response_json);
					return 0;
				}
			}
		}
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Incorrect username or password.");
		send_json_response(401, "Unauthorized", response_json);
		return 1;
	}
	
	// 删除账户 (DELETE action=delete)
	if (strcmp(request_method, "DELETE") == 0 && strcmp(action, "delete") == 0) {
		if (strlen(username) == 0 || strlen(password) == 0) {
			cJSON *response_json = cJSON_CreateObject();
			cJSON_AddStringToObject(response_json, "status", "error");
			cJSON_AddStringToObject(response_json, "message", "Username and password are required.");
			send_json_response(400, "Bad Request", response_json);
			return 1;
		}

		const char *sql_check_user = "SELECT password FROM users WHERE username = ?;";
		rc = sqlite3_prepare_v2(db, sql_check_user, -1, &stmt, 0);
		sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			const char *stored_password = (const char *)sqlite3_column_text(stmt, 0);
			if (strcmp(password, stored_password) == 0) {
				sqlite3_finalize(stmt);

				const char *sql_delete = "DELETE FROM users WHERE username = ?;";
				rc = sqlite3_prepare_v2(db, sql_delete, -1, &stmt, 0);
				sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
				if (sqlite3_step(stmt) == SQLITE_DONE) {
					sqlite3_finalize(stmt);
					sqlite3_close(db);
					cJSON *response_json = cJSON_CreateObject();
					cJSON_AddStringToObject(response_json, "status", "success");
					cJSON_AddStringToObject(response_json, "message", "User deleted successfully.");
					send_json_response(200, "OK", response_json);
					return 0;
				}
			}
		}
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Invalid username or password.");
		send_json_response(401, "Unauthorized", response_json);
		return 1;
	}
	
	// 其他未支持的用户管理请求
	sqlite3_close(db);
	cJSON *response_json = cJSON_CreateObject();
	cJSON_AddStringToObject(response_json, "status", "error");
	cJSON_AddStringToObject(response_json, "message", "Unsupported user management action or method.");
	send_json_response(405, "Method Not Allowed", response_json);
	return 1;
}


int main() {
	// 在处理请求之前，先初始化数据库
	if (init_database() != 0) {
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Failed to initialize database.");
		send_json_response(500, "Internal Server Error", response_json);
		return 1;
	}

	char *request_method = getenv("REQUEST_METHOD");
	char *query_string = getenv("QUERY_STRING");

	if (request_method == NULL) {
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "REQUEST_METHOD not set.");
		send_json_response(500, "Internal Server Error", response_json);
		return 1;
	}

	char action[256] = "";
	if (query_string != NULL) {
		char *action_param = strstr(query_string, "action=");
		if (action_param) {
			char *start = action_param + 7;
			char *end = strchr(start, '&');
			if (end) {
				strncpy(action, start, end - start);
				action[end - start] = '\0';
			} else {
				strncpy(action, start, sizeof(action) - 1);
			}
		}
	}

	// 根据请求方法和 action 参数进行路由
	if (strcmp(request_method, "GET") == 0) {
		// 获取信息
		return handle_get_messages();
	} else if (strcmp(request_method, "POST") == 0) {
		if (strcmp(action, "register") == 0 || strcmp(action, "login") == 0 || strcmp(action, "update") == 0) {
			// 注册或登录
			return handle_user_management(action, request_method);
		} else {
			// 发送消息
			return handle_post_message();
		}
	} else if (strcmp(request_method, "DELETE") == 0) {
		if (strcmp(action, "delete") == 0) {
			// 删除账户
			return handle_user_management(action, request_method);
		} else {
			cJSON *response_json = cJSON_CreateObject();
			cJSON_AddStringToObject(response_json, "status", "error");
			cJSON_AddStringToObject(response_json, "message", "Unsupported DELETE action.");
			send_json_response(405, "Method Not Allowed", response_json);
			return 1;
		}
	} else {
		cJSON *response_json = cJSON_CreateObject();
		cJSON_AddStringToObject(response_json, "status", "error");
		cJSON_AddStringToObject(response_json, "message", "Unsupported request method.");
		send_json_response(405, "Method Not Allowed", response_json);
		return 1;
	}
}
