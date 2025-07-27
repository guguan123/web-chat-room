#!/bin/bash

# 定义 jq 可执行文件的路径
JQ_BIN="$(dirname "$0")/jq"

# 设置CGI头部
echo "Content-type: application/json" # 明确告诉浏览器返回的是 JSON
echo "" # 空行分隔头部和内容

MESSAGES_FILE="/tmp/chat_messages.json"

# 检查文件是否存在并可读
if [[ -f "$MESSAGES_FILE" ]]; then
    # 读取整个 JSON 文件并使用 jq 输出，可以限制只显示最后 50 条消息
    cat "$MESSAGES_FILE" | "$JQ_BIN" '.[-50:]'
else
	# 如果文件不存在，输出空 JSON 数组
	echo "[]"
fi
