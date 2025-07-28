#!/bin/bash

DB_PATH="/tmp/chat_messages.db"

# 检查数据库文件是否存在
if [ ! -f "$DB_PATH" ]; then
    echo "Creating new database at $DB_PATH..."
    sqlite3 "$DB_PATH" <<EOF
CREATE TABLE messages (
    id TEXT PRIMARY KEY,
    timestamp INTEGER,
    ip TEXT,
    username TEXT,
    message TEXT
);
EOF
    chmod 660 "$DB_PATH" # Set appropriate permissions
    echo "Database created successfully."
else
    echo "Database already exists at $DB_PATH. Skipping creation."
fi
