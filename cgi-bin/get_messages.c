#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h> // For time formatting
#include "cJSON.h" // Include cJSON library header

#define DB_PATH "/tmp/chat_messages.db"
#define MAX_MESSAGES 50 // Limit the number of messages to fetch

// Helper function to format timestamp to RFC 3339
void format_timestamp_rfc3339(time_t rawtime, char* buffer, size_t len) {
    struct tm *info;
    info = gmtime(&rawtime); // Use GMT for UTC time
    strftime(buffer, len, "%Y-%m-%dT%H:%M:%SZ", info); // RFC 3339 format
}

int main() {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    // Set CGI header
    printf("Content-type: application/json\r\n\r\n");

    rc = sqlite3_open(DB_PATH, &db);
    if (rc) {
        fprintf(stderr, "{\"error\": \"Can't open database: %s\"}\n", sqlite3_errmsg(db));
        return 1;
    }

    cJSON *root = cJSON_CreateArray();
    if (root == NULL) {
        fprintf(stderr, "{\"error\": \"Failed to create JSON array\"}\n");
        sqlite3_close(db);
        return 1;
    }

    // Select the latest MAX_MESSAGES messages, ordered by ID (which implicitly orders by timestamp due to generation)
    const char *sql = "SELECT id, timestamp, ip, username, message FROM messages ORDER BY id DESC LIMIT ?;";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "{\"error\": \"Failed to prepare statement: %s\"}\n", sqlite3_errmsg(db));
        cJSON_Delete(root);
        sqlite3_close(db);
        return 1;
    }

    sqlite3_bind_int(stmt, 1, MAX_MESSAGES);

    // Fetch messages in reverse order, then add to JSON array in correct chronological order
    cJSON *temp_array = cJSON_CreateArray();
    if (temp_array == NULL) {
        fprintf(stderr, "{\"error\": \"Failed to create temporary JSON array\"}\n");
        cJSON_Delete(root);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 1;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        cJSON *message_obj = cJSON_CreateObject();
        if (message_obj == NULL) {
            fprintf(stderr, "{\"error\": \"Failed to create JSON object for message\"}\n");
            cJSON_Delete(root);
            cJSON_Delete(temp_array);
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            return 1;
        }

        const char *id = (const char *)sqlite3_column_text(stmt, 0);
        time_t timestamp_raw = (time_t)sqlite3_column_int64(stmt, 1);
        const char *ip = (const char *)sqlite3_column_text(stmt, 2);
        const char *username = (const char *)sqlite3_column_text(stmt, 3);
        const char *message = (const char *)sqlite3_column_text(stmt, 4);

        char timestamp_str[30]; // Sufficient for RFC 3339
        format_timestamp_rfc3339(timestamp_raw, timestamp_str, sizeof(timestamp_str));

        cJSON_AddStringToObject(message_obj, "id", id);
        cJSON_AddStringToObject(message_obj, "timestamp", timestamp_str);
        cJSON_AddStringToObject(message_obj, "ip", ip);
        cJSON_AddStringToObject(message_obj, "username", username);
        cJSON_AddStringToObject(message_obj, "message", message);

        cJSON_AddItemToArray(temp_array, message_obj);
    }

    // Add messages from temp_array to root in chronological order
    for (int i = cJSON_GetArraySize(temp_array) - 1; i >= 0; i--) {
        cJSON_AddItemToArray(root, cJSON_DetachItemFromArray(temp_array, i));
    }
    cJSON_Delete(temp_array); // Free the temporary array

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    char *json_output = cJSON_PrintUnformatted(root);
    if (json_output != NULL) {
        printf("%s\n", json_output);
        free(json_output);
    } else {
        fprintf(stderr, "{\"error\": \"Failed to print JSON\"}\n");
    }

    cJSON_Delete(root);

    return 0;
}
