#!/bin/bash

# 设置CGI头部，Content-type必须在第一行
echo "Content-type: text/plain"
echo "" # 空行分隔头部和内容

# 确保消息文件存在，并设置适当的权限
MESSAGES_FILE="/tmp/chat_messages.txt" # 聊天消息存储文件
# 为了安全和简单的考虑，这里使用 /tmp。在生产环境中，你应该选择一个更安全、有适当权限的目录。
touch "$MESSAGES_FILE"
chmod 666 "$MESSAGES_FILE" # 允许所有用户读写，方便调试，生产环境需更严格

# 读取POST请求体中的数据
read -r QUERY_STRING

# 解析表单数据 (非常简陋的解析方式)
# 假设数据格式是 key1=value1&key2=value2
username=""
message=""

IFS='&' read -ra ADDR <<< "$QUERY_STRING"
for i in "${ADDR[@]}"; do
    KEY=$(echo "$i" | cut -d'=' -f1)
    VALUE=$(echo "$i" | cut -d'=' -f2-)
    
    # URL解码
    DECODED_VALUE=$(echo "$VALUE" | sed -r 's/%(..)/\\x\1/g' | xargs -0 printf "%b")

    case "$KEY" in
        "username") username="$DECODED_VALUE" ;;
        "message") message="$DECODED_VALUE" ;;
    esac
done

# 获取当前时间戳
TIMESTAMP=$(date +"%Y-%m-%d %H:%M:%S")

# 将消息写入文件
# 为了避免消息内容中的换行符或特殊字符影响格式，这里只取第一行
# 并且对用户名和消息内容进行简单的清理，移除潜在的换行符
CLEAN_USERNAME=$(echo "$username" | tr '\n' ' ' | head -n 1)
CLEAN_MESSAGE=$(echo "$message" | tr '\n' ' ' | head -n 1)

if [[ -n "$CLEAN_MESSAGE" ]]; then # 确保消息不为空
    echo "${TIMESTAMP}::${CLEAN_USERNAME}::${CLEAN_MESSAGE}" >> "$MESSAGES_FILE"
    echo "OK: Message posted."
else
    echo "Error: Message is empty."
fi
