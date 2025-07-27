#!/bin/bash
# CGI脚本：处理消息发送
echo "Content-type: text/plain"
echo ""

# 读取POST数据
read -r POST_DATA

# 解析username和message
username=$(echo "$POST_DATA" | grep -oP '(?<=username=)[^&]*' | sed 's/%20/ /g')
message=$(echo "$POST_DATA" | grep -oP '(?<=message=)[^&]*' | sed 's/%20/ /g')

# 获取当前时间戳
timestamp=$(date '+%Y-%m-%d %H:%M:%S')

# 消息文件路径
MESSAGE_FILE="/var/localhost/htdocs/messages.txt"

# 追加消息到文件
if [ -n "$username" ] && [ -n "$message" ]; then
    echo "[$timestamp] $username: $message" >> "$MESSAGE_FILE"
    echo "消息发送成功"
else
    echo "错误：用户名或消息为空"
fi
