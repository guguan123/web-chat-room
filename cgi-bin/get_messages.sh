#!/bin/bash

# 设置CGI头部
echo "Content-type: text/plain"
echo "" # 空行分隔头部和内容

MESSAGES_FILE="/tmp/chat_messages.txt" # 聊天消息存储文件

# 检查文件是否存在并可读
if [[ -f "$MESSAGES_FILE" ]]; then
    # 只显示最后 50 条消息，与前端显示保持一致
    tail -n 50 "$MESSAGES_FILE"
else
    echo "" # 如果文件不存在，则不输出任何内容
fi
