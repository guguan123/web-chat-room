#!/bin/bash
# CGI脚本：获取消息
echo "Content-type: text/plain"
echo ""

# 消息文件路径
MESSAGE_FILE="/var/localhost/htdocs/messages.txt"

# 读取并输出消息
if [ -f "$MESSAGE_FILE" ]; then
    cat "$MESSAGE_FILE"
else
    echo "暂无消息"
fi
