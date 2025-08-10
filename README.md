# web-chat-room

一个非常简单的聊天室

## 使用方法

1、下载最新 release

2、设置 CGI 和 HTTPD 程序可执行权限

```bash
chmod +x cgi-bin/chat_handler.cgi
chmod +x busybox_HTTPD
```

3、运行 busybox_HTTPD

```bash
./busybox_HTTPD -f -p 8080
```

此时就可以打开 <http://localhost:8080/chat.html>

## 手动编译

```bash
(cd cgi-bin && make)
```
