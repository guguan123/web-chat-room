#!/bin/bash

# 定义 jq 可执行文件的路径
JQ_BIN="$(dirname "$0")/jq"

# 设置CGI头部
echo "Content-type: text/plain"
echo "" # 空行分隔头部和内容

MESSAGES_FILE="/tmp/chat_messages.json"

# 确保文件存在且权限正确，初始化为[]如果不存在
if [[ ! -f "$MESSAGES_FILE" || ! -s "$MESSAGES_FILE" ]]; then
    echo "[]" > "$MESSAGES_FILE"
	chmod 660 "$MESSAGES_FILE" # 确保权限正确
fi

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

# 消息长度限制
MAX_MESSAGE_LENGTH=1024
if [[ ${#CLEAN_MESSAGE} -gt $MAX_MESSAGE_LENGTH ]]; then
    CLEAN_MESSAGE=$(echo "$CLEAN_MESSAGE" | cut -c 1-$MAX_MESSAGE_LENGTH)
    echo "Warning: Message truncated to $MAX_MESSAGE_LENGTH characters."
fi

# 旧消息最大保存条数 (保留最新MAX_MESSAGES条)
MAX_MESSAGES=200

if [[ -n "$CLEAN_MESSAGE" ]]; then
	# 使用时间戳和随机数生成唯一ID
	MESSAGE_ID="msg_$(date +%s%N)_$RANDOM"

	# 构建新的消息 JSON 对象
	json_new_message=$(
		echo "{}" | "$JQ_BIN" \
			--arg id "$MESSAGE_ID" \
			--arg ts "$TIMESTAMP" \
			--arg ip "$USER_IP" \
			--arg uname "$CLEAN_USERNAME" \
			--arg msg "$CLEAN_MESSAGE" \
			'. + {id: $id, timestamp: $ts, ip: $ip, username: $uname, message: $msg}' | tr -d '\n'
	)

	if [[ -n "$json_new_message" ]]; then
		temp_file=$(mktemp)

		# 读取现有消息，追加新消息，并清理旧消息，然后一次性写回
		# 使用 cat "$MESSAGES_FILE" | "$JQ_BIN" ... 是为了处理文件不存在或空文件的情况
		# jq 表达式:
		# . + [$json_new_message]  -> 追加新消息
		# | .[ -($MAX_MESSAGES) : ] -> 截取数组的最后 MAX_MESSAGES 条
		cat "$MESSAGES_FILE" | "$JQ_BIN" \
			--argjson new_msg "$json_new_message" \
			--argjson max_len "$MAX_MESSAGES" \
			'. + [$new_msg] | .[ -($max_len) : ]' > "$temp_file" && mv "$temp_file" "$MESSAGES_FILE"

		echo "OK: Message posted and old messages cleaned."
	else
		echo "Error: Failed to create JSON object with jq."
	fi
else
	echo "Error: Message is empty."
fi
