#!/bin/bash

# 设置CGI头部
echo "Content-type: text/plain"
echo "" # 空行分隔头部和内容

MESSAGES_FILE="/tmp/chat_messages.txt" # 聊天消息存储文件

# 检查文件是否存在并可读
if [[ -f "$MESSAGES_FILE" ]]; then
    # 限制只显示最后50条消息，避免文件过大影响性能
    tail -n 50 "$MESSAGES_FILE"
else
    echo "" # 如果文件不存在，则不输出任何内容
fi
