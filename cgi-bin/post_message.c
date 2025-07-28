#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

#define DB_PATH "/tmp/chat_messages.db"
#define MAX_MESSAGE_LENGTH 1024
#define MAX_MESSAGES 200

// Function to URL-decode a string (simplified for CGI context)
void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= '0' && a <= '9') a -= '0';
            else a -= 'A' - 10;
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= '0' && b <= '9') b -= '0';
            else b -= 'A' - 10;
            *dst++ = 16*a + b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

int main() {
    // Set CGI header
    printf("Content-type: text/plain\r\n\r\n");

    char *query_string = getenv("QUERY_STRING");
    char username[256] = "";
    char message[MAX_MESSAGE_LENGTH + 1] = "";
    char decoded_value[MAX_MESSAGE_LENGTH + 1];

    if (query_string != NULL) {
        char *token;
        char *rest = query_string;
        while ((token = strtok_r(rest, "&", &rest))) {
            char *key = token;
            char *value = strchr(token, '=');
            if (value) {
                *value = '\0'; // Null-terminate key
                value++; // Move past '='
                url_decode(decoded_value, value); // Decode value
                if (strcmp(key, "username") == 0) {
                    strncpy(username, decoded_value, sizeof(username) - 1);
                    username[sizeof(username) - 1] = '\0';
                } else if (strcmp(key, "message") == 0) {
                    strncpy(message, decoded_value, sizeof(message) - 1);
                    message[sizeof(message) - 1] = '\0';
                }
            }
        }
    }

    if (strlen(message) == 0) {
        printf("Error: Message is empty.\n");
        return 1;
    }

    if (strlen(message) > MAX_MESSAGE_LENGTH) {
        message[MAX_MESSAGE_LENGTH] = '\0'; // Truncate
        printf("Warning: Message truncated to %d characters.\n", MAX_MESSAGE_LENGTH);
    }

    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_open(DB_PATH, &db);
    if (rc) {
        fprintf(stderr, "Error: Can't open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // Get user IP (simplified, actual implementation might need more robust parsing)
    const char *user_ip = getenv("HTTP_CF_CONNECTING_IP");
    if (user_ip == NULL) {
        user_ip = getenv("REMOTE_ADDR");
    }
    if (user_ip == NULL || strlen(user_ip) == 0) {
        user_ip = "UNKNOWN_IP";
    }

    // Generate unique ID based on timestamp and a simple counter (or UUID if preferred)
    // For simplicity, using current time in milliseconds as part of ID, and also as the timestamp value.
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long current_time_ms = (long long)ts.tv_sec * 1000 + (long long)ts.tv_nsec / 1000000;

    char message_id[64];
    snprintf(message_id, sizeof(message_id), "msg_%lld", current_time_ms);

    // Insert new message
    const char *sql_insert = "INSERT INTO messages (id, timestamp, ip, username, message) VALUES (?, ?, ?, ?, ?);";
    rc = sqlite3_prepare_v2(db, sql_insert, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to prepare insert statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    sqlite3_bind_text(stmt, 1, message_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, ts.tv_sec); // Store timestamp as Unix epoch seconds
    sqlite3_bind_text(stmt, 3, user_ip, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, message, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error: Failed to execute insert statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 1;
    }
    sqlite3_finalize(stmt);

    // Clean up old messages (keep only MAX_MESSAGES)
    const char *sql_delete_old = "DELETE FROM messages WHERE id NOT IN (SELECT id FROM messages ORDER BY id DESC LIMIT ?);";
    rc = sqlite3_prepare_v2(db, sql_delete_old, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error: Failed to prepare delete statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    sqlite3_bind_int(stmt, 1, MAX_MESSAGES);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Error: Failed to execute delete statement: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 1;
    }
    sqlite3_finalize(stmt);

    sqlite3_close(db);

    printf("OK: Message posted and old messages cleaned.\n");

    return 0;
}
