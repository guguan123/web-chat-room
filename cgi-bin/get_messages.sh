#!/bin/bash

# 定义 jq 可执行文件的路径
JQ_BIN="$(dirname "$0")/jq"

# 设置CGI头部
echo "Content-type: application/json" # 明确告诉浏览器返回的是 JSON
echo "" # 空行分隔头部和内容

MESSAGES_FILE="/tmp/chat_messages.txt"

# 检查文件是否存在并可读
if [[ -f "$MESSAGES_FILE" ]]; then
    # 限制只显示最后 50 条消息
    # 将文件内容通过管道传递给 jq，让 jq 作为一个整体处理 JSON 行
    # -s 选项将所有输入合并为一个数组
    # .[] 遍历数组中的每个元素
    # 如果文件是多行JSON，jq -s . 就能直接将所有行读取成一个JSON数组
    "$JQ_BIN" -s '.' <(tail -n 50 "$MESSAGES_FILE")
else
    # 如果文件不存在，输出空 JSON 数组
    echo "[]"
fi
