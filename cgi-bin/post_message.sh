#!/bin/bash

# 定义 jq 可执行文件的路径
JQ_BIN="$(dirname "$0")/jq"

# 设置CGI头部
echo "Content-type: text/plain"
echo "" # 空行分隔头部和内容

MESSAGES_FILE="/tmp/chat_messages.txt"

touch "$MESSAGES_FILE"
chmod 660 "$MESSAGES_FILE" # 确保权限正确

read -r QUERY_STRING

username=""
message=""

IFS='&' read -ra ADDR <<< "$QUERY_STRING"
for i in "${ADDR[@]}"; do
	KEY=$(echo "$i" | cut -d'=' -f1)
	VALUE=$(echo "$i" | cut -d'=' -f2-)
	
	# URL解码 (这部分不变，因为是处理URL编码的POST数据)
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
	# *** 关键改动：使用 jq 构建 JSON 对象 ***
	# 将键值对通过 echo 传递给 jq，让 jq 构造 JSON
	# 使用 -c 选项确保输出为紧凑的单行 JSON
	json_object=$(
		echo "{}" | "$JQ_BIN" \
			--arg ts "$TIMESTAMP" \
			--arg ip "$USER_IP" \
			--arg uname "$CLEAN_USERNAME" \
			--arg msg "$CLEAN_MESSAGE" \
			'. + {timestamp: $ts, ip: $ip, username: $uname, message: $msg}' | tr -d '\n'
	)

	if [[ -n "$json_object" ]]; then
		echo "$json_object" >> "$MESSAGES_FILE"
		echo "OK: Message posted."
	else
		echo "Error: Failed to create JSON object with jq."
	fi
else
	echo "Error: Message is empty."
fi

MAX_MESSAGES=200
CURRENT_LINES=$(wc -l < "$MESSAGES_FILE")

if (( CURRENT_LINES > MAX_MESSAGES )); then
	tail -n "$MAX_MESSAGES" "$MESSAGES_FILE" > "${MESSAGES_FILE}.tmp" && mv "${MESSAGES_FILE}.tmp" "$MESSAGES_FILE"
	echo "Cleaned old messages."
fi
