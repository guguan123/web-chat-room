#!/bin/bash

# 加载 JSON.sh 库
. "$(dirname "$0")/JSON.sh"

# 设置CGI头部
echo "Content-type: text/plain" # 也可以设置为 application/json 如果前端需要更严格的检查
echo "" # 空行分隔头部和内容

MESSAGES_FILE="/tmp/chat_messages.txt"

touch "$MESSAGES_FILE"
chmod 660 "$MESSAGES_FILE"

read -r QUERY_STRING

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

USER_IP=${HTTP_CF_CONNECTING_IP:-$REMOTE_ADDR}
USER_IP=$(echo "$USER_IP" | tr '\n' ' ' | head -n 1 | cut -d' ' -f1)
if [[ -z "$USER_IP" ]]; then
    USER_IP="UNKNOWN_IP"
fi

TIMESTAMP=$(date +"%Y-%m-%d %H:%M:%S")

CLEAN_USERNAME=$(echo "$username" | tr '\n' ' ' | head -n 1)
CLEAN_MESSAGE=$(echo "$message" | tr '\n' ' ' | head -n 1)

if [[ -n "$CLEAN_MESSAGE" ]]; then
    # *** 关键改动：构建 JSON 对象并写入文件 ***
    # 使用 JSON.sh 的 json_encode 函数
    # 注意：这里的 json_encode 期望 key 和 value 都单独传递
    # json_encode "$KEY1" "$VALUE1" "$KEY2" "$VALUE2" ...
    json_object=$(json_encode \
        "timestamp" "$TIMESTAMP" \
        "ip" "$USER_IP" \
        "username" "$CLEAN_USERNAME" \
        "message" "$CLEAN_MESSAGE")

    echo "$json_object" >> "$MESSAGES_FILE"
    echo "OK: Message posted."
else
    echo "Error: Message is empty."
fi

MAX_MESSAGES=200
CURRENT_LINES=$(wc -l < "$MESSAGES_FILE")

if (( CURRENT_LINES > MAX_MESSAGES )); then
    tail -n "$MAX_MESSAGES" "$MESSAGES_FILE" > "${MESSAGES_FILE}.tmp" && mv "${MESSAGES_FILE}.tmp" "$MESSAGES_FILE"
    echo "Cleaned old messages."
fi
