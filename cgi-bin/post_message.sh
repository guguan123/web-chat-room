#!/bin/bash

# 设置CGI头部
echo "Content-type: text/plain"
echo "" # 空行分隔头部和内容

MESSAGES_FILE="/tmp/chat_messages.txt" # 聊天消息存储文件
# 在生产环境中，强烈建议将此文件放在一个非公开访问、有适当权限的目录。
# 例如：/var/lib/my_chat_app/chat_messages.txt
# 确保Web服务器用户（如www-data）对此目录和文件有写入权限。

# 确保消息文件存在，并设置适当的权限
touch "$MESSAGES_FILE"
chmod 660 "$MESSAGES_FILE" # 给予所有者和组写入权限，其他用户无权限。
                          # 组需是Web服务器运行的用户组。

# 读取POST请求体中的数据
read -r QUERY_STRING

# 解析表单数据 (简陋解析方式，同前)
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

# 获取真实用户IP地址，优先从 CF-Connecting-IP 获取
# 如果没有 CF-Connecting-IP，则尝试获取 REMOTE_ADDR
# 注意：REMOTE_ADDR 可能是CDN的IP，而不是真实用户IP
USER_IP=${HTTP_CF_CONNECTING_IP:-$REMOTE_ADDR}
# 对IP进行简单清理，防止注入或多行
USER_IP=$(echo "$USER_IP" | tr '\n' ' ' | head -n 1 | cut -d' ' -f1)
if [[ -z "$USER_IP" ]]; then
    USER_IP="UNKNOWN_IP"
fi


# 获取当前时间戳
TIMESTAMP=$(date +"%Y-%m-%d %H:%M:%S")

# 对用户名和消息内容进行简单的清理，移除潜在的换行符
CLEAN_USERNAME=$(echo "$username" | tr '\n' ' ' | head -n 1)
CLEAN_MESSAGE=$(echo "$message" | tr '\n' ' ' | head -n 1)

if [[ -n "$CLEAN_MESSAGE" ]]; then # 确保消息不为空
    # 消息格式：时间戳::IP::用户名::消息内容
    echo "${TIMESTAMP}::${USER_IP}::${CLEAN_USERNAME}::${CLEAN_MESSAGE}" >> "$MESSAGES_FILE"
    echo "OK: Message posted."
else
    echo "Error: Message is empty."
fi

# *** 简单的日志文件清理机制 ***
# 只保留最新的200条消息，防止文件无限增大占用过多存储空间和读取性能
# 这会在每次写入新消息时执行，对单核CPU有一定压力，但用户量不大时可接受。
MAX_MESSAGES=200
CURRENT_LINES=$(wc -l < "$MESSAGES_FILE")

if (( CURRENT_LINES > MAX_MESSAGES )); then
    # 使用 tail -n 加上一个偏移量，防止在文件非常大时截断过多
    # 这里我们直接取最后 MAX_MESSAGES 条，然后覆盖原文件
    tail -n "$MAX_MESSAGES" "$MESSAGES_FILE" > "${MESSAGES_FILE}.tmp" && mv "${MESSAGES_FILE}.tmp" "$MESSAGES_FILE"
    # 错误处理：如果mv失败，原文件仍在，不影响基本功能
    echo "Cleaned old messages."
fi
