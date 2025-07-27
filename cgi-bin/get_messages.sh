#!/bin/bash

# 加载 JSON.sh 库
. "$(dirname "$0")/JSON.sh"

# 设置CGI头部
echo "Content-type: application/json" # 明确告诉浏览器返回的是 JSON
echo "" # 空行分隔头部和内容

MESSAGES_FILE="/tmp/chat_messages.txt"

messages_array="[" # 初始化 JSON 数组

# 检查文件是否存在并可读
if [[ -f "$MESSAGES_FILE" ]]; then
    # 限制只显示最后 50 条消息
    # 逐行读取 JSON 字符串
    tail -n 50 "$MESSAGES_FILE" | while IFS= read -r line; do
        if [[ -n "$line" ]]; then
            # 验证 JSON 有效性，并将其添加到数组
            # JSON.sh 默认输出是 key=value 形式，我们需要将其重新组合成 JSON 对象
            # 更好的方法是直接将读取到的原始 JSON 行添加到数组
            if [[ "$messages_array" != "[" ]]; then
                messages_array+="," # 如果不是第一个元素，添加逗号
            fi
            messages_array+="$line" # 直接追加原始 JSON 对象
        fi
    done
fi

messages_array+="]" # 结束 JSON 数组

echo "$messages_array"
