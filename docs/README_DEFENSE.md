# Lume 静态 Web 服务器答辩说明文档

本文面向第一次接触该项目的读者，用于答辩前快速理解 Lume 服务器的主体架构、请求处理流程、模块职责和关键函数作用。

## 1. 项目一句话介绍

Lume 是一个使用 C 语言编写、运行在 Linux 上的轻量级静态 Web 服务器。它通过 `epoll` 实现单线程 Reactor 事件驱动模型，可以响应浏览器发来的 HTTP GET 请求，并返回 HTML、CSS、JavaScript、图片等静态资源。

简单理解：

```text
浏览器请求 URL
    |
    v
Lume 服务器解析 HTTP 请求
    |
    v
从 ROOT_DIR 中找到对应静态文件
    |
    v
构造 HTTP 响应并返回给浏览器
```

## 2. 项目解决的问题

本项目主要解决三个学习和工程问题：

- 理解 Linux 网络服务器如何从 socket 开始接收浏览器请求。
- 理解 HTTP 请求和响应的基础格式。
- 理解高并发服务器为什么使用 epoll 和非阻塞 I/O。

当前服务器支持：

- HTTP GET 请求。
- 静态文件访问。
- HTTP 状态码 `200`、`400`、`404`、`501`。
- MIME 类型识别。
- 配置文件加载。
- 目录遍历防护。
- epoll 单线程事件循环。
- 最大请求头、最大连接数、空闲连接超时限制。

当前服务器暂不支持：

- HTTPS。
- HTTP keep-alive 长连接。
- 动态后端接口。
- 数据库访问。
- CGI。
- `sendfile` 零拷贝优化。

## 3. 目录结构总览

```text
.
├── CMakeLists.txt
├── config/
│   └── server.conf
├── include/lume/
│   ├── buffer.h
│   ├── config.h
│   ├── connection.h
│   ├── event_loop.h
│   ├── http.h
│   ├── log.h
│   ├── mime.h
│   ├── server.h
│   └── static_file.h
├── src/
│   ├── main.c
│   ├── config.c
│   ├── server.c
│   ├── event_loop.c
│   ├── connection.c
│   ├── http.c
│   ├── static_file.c
│   ├── mime.c
│   ├── buffer.c
│   └── log.c
├── public/
│   ├── index.html
│   ├── style.css
│   └── app.js
├── tests/
├── scripts/
└── docs/
```

各目录作用：

| 目录 | 作用 |
| --- | --- |
| `src/` | C 源码实现 |
| `include/lume/` | 模块头文件，声明对外接口 |
| `config/` | 服务器配置文件 |
| `public/` | 默认静态网站根目录 |
| `tests/` | 单元测试 |
| `scripts/` | 冒烟测试和压测脚本 |
| `docs/` | 项目文档 |

## 4. 编译、运行和访问

编译：

```bash
cmake -S . -B build
cmake --build build
```

运行：

```bash
./build/lume_server config/server.conf
```

浏览器访问：

```text
http://127.0.0.1:8080/
```

配置文件：

```conf
PORT=8080
ROOT_DIR=public

MAX_REQUEST_BYTES=16384
MAX_CONNECTIONS=4096
REQUEST_TIMEOUT=30
```

配置含义：

| 配置 | 作用 |
| --- | --- |
| `PORT` | 服务器监听端口 |
| `ROOT_DIR` | 静态文件根目录 |
| `MAX_REQUEST_BYTES` | 最大请求头大小 |
| `MAX_CONNECTIONS` | 最大活跃连接数 |
| `REQUEST_TIMEOUT` | 空闲连接超时时间 |

## 5. 总体架构

项目采用分层模块化结构：

```text
main
  |
  v
config 读取配置
  |
  v
server 创建监听 socket
  |
  v
event_loop 启动 epoll 主循环
  |
  v
connection 管理客户端连接
  |
  +--> http 解析请求和构造响应
  |
  +--> static_file 查找并打开静态文件
  |
  +--> mime 判断 Content-Type
  |
  +--> buffer 保存响应头和发送进度
```

核心思想：

- `server` 只负责创建监听 socket。
- `event_loop` 只负责等待和分发事件。
- `connection` 只负责单个客户端连接的生命周期。
- `http` 只负责 HTTP 协议相关逻辑。
- `static_file` 只负责 URI 到本地文件的安全映射。
- `mime` 只负责文件类型判断。
- `buffer` 只负责动态缓冲区。

这样的设计让每个模块职责单一，后续修改和扩展更容易。

## 6. 一次请求的完整流程

以浏览器访问首页为例：

```text
http://127.0.0.1:8080/
```

完整流程如下：

```text
1. main.c 启动程序
2. config.c 读取 config/server.conf
3. server.c 创建监听 socket，绑定 8080 端口
4. event_loop.c 把监听 socket 加入 epoll
5. 浏览器连接服务器
6. event_loop.c 收到监听 socket 可读事件
7. accept4 接收新连接
8. connection.c 创建连接对象并注册到 epoll
9. 浏览器发送 HTTP 请求
10. connection.c 读取请求数据
11. http.c 解析请求行
12. static_file.c 将 URI 映射到 public/index.html
13. mime.c 判断文件类型是 text/html
14. http.c 构造 HTTP 响应头
15. connection.c 发送响应头
16. connection.c 分块发送文件内容
17. 响应完成后关闭连接
```

## 7. Reactor 和 epoll 如何工作

### 7.1 为什么不用阻塞 I/O

如果服务器使用普通阻塞 I/O，一个客户端很慢时，服务器可能卡在 `read` 或 `write` 上，影响其他客户端。

为了解决这个问题，本项目使用：

- 非阻塞 socket。
- epoll I/O 多路复用。
- 单线程 Reactor 模型。

### 7.2 epoll 的作用

`epoll` 可以让服务器同时监听多个 fd：

- 监听 socket fd：用于接收新连接。
- 客户端 socket fd：用于读取请求或发送响应。

当某个 fd 可读或可写时，`epoll_wait` 会返回事件，程序再处理对应 fd。

### 7.3 本项目中的事件类型

```text
LUME_EVENT_LISTENER    -> 监听 socket 事件
LUME_EVENT_CONNECTION  -> 客户端连接事件
```

在 `event_loop.c` 中会根据事件类型分发：

- 如果是监听 socket，调用 `accept_connections`。
- 如果是客户端连接，调用 `lume_connection_handle`。

## 8. 连接状态机

单个客户端连接由 `connection.c` 管理。

连接状态：

```text
CONNECTION_READING         正在读取请求
CONNECTION_PROCESSING      正在解析请求并准备响应
CONNECTION_WRITING_HEADER  正在发送响应头
CONNECTION_WRITING_FILE    正在发送静态文件内容
CONNECTION_CLOSING         准备关闭连接
```

状态流转：

```text
READING
  |
  v
PROCESSING
  |
  v
WRITING_HEADER
  |
  +-- 如果有静态文件 --> WRITING_FILE
  |
  +-- 如果是错误响应 --> CLOSING
  |
  v
CLOSING
```

为什么要设计状态机：

- 读请求和写响应是不同阶段。
- 非阻塞 I/O 可能一次读不完或写不完。
- 大文件发送需要保存发送进度。
- 状态清晰后，epoll 可以根据状态切换 `EPOLLIN` 或 `EPOLLOUT`。

## 9. 各模块详细说明

### 9.1 `main.c` 程序入口

主要函数：

| 函数 | 作用 |
| --- | --- |
| `main` | 程序入口，读取配置文件路径，加载配置，初始化服务器并启动事件循环 |

主要流程：

```text
忽略 SIGPIPE
  |
  v
加载 config/server.conf
  |
  v
初始化服务器
  |
  v
运行 epoll 事件循环
```

为什么忽略 `SIGPIPE`：

如果客户端提前断开连接，服务器继续写 socket 时可能收到 `SIGPIPE` 信号。默认情况下该信号会导致进程退出，所以这里忽略它，提高服务器稳定性。

### 9.2 `config.c` 配置模块

配置模块负责读取 `config/server.conf`。

主要函数：

| 函数 | 作用 |
| --- | --- |
| `lume_config_init` | 初始化默认配置 |
| `lume_config_load` | 从配置文件加载配置，并校验合法性 |
| `set_error` | 写入错误信息，便于启动失败时输出原因 |
| `trim` | 去掉字符串首尾空白字符 |
| `parse_long` | 解析整数并检查范围 |
| `parse_port` | 解析端口，范围必须是 1 到 65535 |
| `parse_size_value` | 解析大小类配置，例如最大请求头和最大连接数 |

配置模块做的校验：

- `PORT` 必须合法。
- `ROOT_DIR` 必须存在。
- `ROOT_DIR` 会通过 `realpath` 转成绝对路径。
- `MAX_REQUEST_BYTES`、`MAX_CONNECTIONS`、`REQUEST_TIMEOUT` 必须在合理范围内。

### 9.3 `server.c` 服务器初始化模块

该模块负责创建监听 socket。

主要函数：

| 函数 | 作用 |
| --- | --- |
| `lume_set_nonblocking` | 将 fd 设置为非阻塞 |
| `lume_server_init` | 创建 socket、设置端口复用、绑定端口、开始监听 |
| `lume_server_run` | 调用事件循环开始运行服务器 |
| `lume_server_destroy` | 关闭 epoll fd 和监听 fd |

关键系统调用：

```text
socket
setsockopt
bind
listen
fcntl
close
```

答辩可讲重点：

- `SO_REUSEADDR` 用于方便服务重启。
- `SOCK_NONBLOCK` 用于创建非阻塞 socket。
- 监听 socket 只负责接收连接，不直接处理 HTTP。

### 9.4 `event_loop.c` 事件循环模块

这是 Reactor 模型的核心。

主要函数：

| 函数 | 作用 |
| --- | --- |
| `lume_event_loop_run` | 创建 epoll，注册监听 fd，进入主事件循环 |
| `register_listener` | 将监听 socket 注册到 epoll |
| `accept_connections` | 循环接受新客户端连接，直到 `EAGAIN` |
| `track_connection` | 将连接加入链表，用于统计和超时清理 |
| `untrack_connection` | 从连接链表中移除连接 |
| `close_connection` | 销毁连接并减少活跃连接数 |
| `prune_idle_connections` | 定期关闭超过超时时间的空闲连接 |

主循环逻辑：

```text
epoll_wait 等待事件
  |
  v
遍历返回事件
  |
  +-- 监听 fd 事件：accept 新连接
  |
  +-- 客户端 fd 事件：交给 connection 处理
  |
  v
扫描并关闭空闲连接
```

答辩可讲重点：

- `accept_connections` 使用循环，直到返回 `EAGAIN`。
- 活跃连接数不能超过 `MAX_CONNECTIONS`。
- `epoll_wait` 使用 1 秒超时，便于定期清理空闲连接。

### 9.5 `connection.c` 连接管理模块

这是项目中最核心的业务模块，负责一个客户端连接从创建到关闭的全过程。

主要数据结构：

```c
struct lume_connection {
    int fd;
    int epoll_fd;
    const lume_config *config;
    connection_state state;
    time_t last_active_at;
    lume_buffer read_buffer;
    lume_buffer header_buffer;
    int file_fd;
    off_t file_size;
    off_t file_sent;
    char file_chunk[LUME_FILE_CHUNK_SIZE];
    size_t file_chunk_length;
    size_t file_chunk_offset;
};
```

字段含义：

| 字段 | 作用 |
| --- | --- |
| `fd` | 当前客户端 socket |
| `epoll_fd` | epoll 实例 fd |
| `config` | 指向全局配置 |
| `state` | 当前连接状态 |
| `last_active_at` | 最近活跃时间，用于超时判断 |
| `read_buffer` | 保存客户端请求 |
| `header_buffer` | 保存响应头或错误响应 |
| `file_fd` | 当前要发送的静态文件 |
| `file_size` | 文件总大小 |
| `file_sent` | 已发送文件字节数 |
| `file_chunk` | 文件分块缓冲区 |
| `file_chunk_offset` | 当前块已发送偏移 |

主要函数：

| 函数 | 作用 |
| --- | --- |
| `lume_connection_create` | 创建并初始化连接对象 |
| `lume_connection_destroy` | 注销 epoll 事件、关闭 fd、释放缓冲区和连接内存 |
| `lume_connection_register` | 将客户端 fd 注册到 epoll |
| `lume_connection_handle` | 根据 epoll 事件和连接状态处理读写 |
| `lume_connection_is_idle_expired` | 判断连接是否超时 |
| `mark_active` | 更新连接最近活跃时间 |
| `events_for_state` | 根据连接状态决定监听 `EPOLLIN` 还是 `EPOLLOUT` |
| `update_events` | 修改 epoll 中的监听事件 |
| `set_state` | 修改连接状态并同步 epoll 事件 |
| `request_headers_complete` | 判断 HTTP 请求头是否已经读取完整 |
| `prepare_error_response` | 构造错误响应，例如 400、404、501 |
| `prepare_static_response` | 打开静态文件并构造 200 响应头 |
| `process_request` | 解析请求并决定返回错误还是文件 |
| `transition_to_processing` | 从读取阶段进入处理阶段 |
| `read_request` | 非阻塞读取客户端请求 |
| `flush_header` | 非阻塞发送响应头 |
| `load_next_file_chunk` | 从文件读取下一块内容 |
| `flush_file_chunk` | 非阻塞发送当前文件块 |
| `send_file` | 循环发送完整文件 |
| `handle_read` | 处理读事件 |
| `handle_write_header` | 处理响应头写事件 |
| `handle_write_file` | 处理文件写事件 |

答辩可讲重点：

- 连接模块是状态机，不是简单的一次 read/write。
- 大文件不会一次性读入内存，而是分块发送。
- 写 socket 可能一次写不完，所以要保存偏移。
- 请求头过大时返回 `400 Bad Request`。
- 空闲连接会被事件循环定期清理。

### 9.6 `http.c` HTTP 协议模块

该模块负责 HTTP 请求解析和响应构造。

主要函数：

| 函数 | 作用 |
| --- | --- |
| `lume_http_parse_request` | 解析 HTTP 请求行，提取方法、URI、版本 |
| `lume_http_status_reason` | 根据状态码返回原因短语 |
| `lume_http_append_response_headers` | 构造标准 HTTP 响应头 |
| `lume_http_append_error_response` | 构造错误响应头和 HTML 错误页面 |
| `find_request_line_end` | 查找请求行结尾 |

支持的请求示例：

```http
GET /index.html HTTP/1.1
```

支持的状态码：

| 状态码 | 含义 |
| --- | --- |
| `200 OK` | 文件存在，正常返回 |
| `400 Bad Request` | 请求格式错误或路径非法 |
| `404 Not Found` | 文件不存在 |
| `501 Not Implemented` | 请求方法不是 GET |

响应头示例：

```http
HTTP/1.1 200 OK
Server: Lume-C
Content-Length: 123
Content-Type: text/html; charset=utf-8
Connection: close
```

### 9.7 `static_file.c` 静态文件模块

该模块负责把 URI 安全地映射到本地文件。

主要函数：

| 函数 | 作用 |
| --- | --- |
| `lume_static_file_init` | 初始化静态文件结构 |
| `lume_static_file_close` | 关闭文件 fd 并清空结构 |
| `lume_static_file_open` | 根据 ROOT_DIR 和 URI 打开静态文件 |
| `hex_value` | 解析 URL 编码中的十六进制字符 |
| `decode_uri_path` | 解码 URI 中的 `%xx` 编码 |
| `append_segment` | 拼接安全路径片段 |
| `normalize_decoded_path` | 规范化路径，处理 `/` 和 `index.html` |
| `path_is_inside_root` | 判断最终路径是否仍在 ROOT_DIR 内 |

路径映射示例：

```text
URI: /
文件: public/index.html

URI: /style.css
文件: public/style.css

URI: /images/logo.png
文件: public/images/logo.png
```

安全防护：

- 拒绝 `..`。
- 拒绝非法 `%xx` 编码。
- 拒绝空字节。
- 拒绝控制字符。
- 拒绝反斜杠。
- 使用 `realpath` 确认最终文件没有逃出 `ROOT_DIR`。

答辩可讲重点：

- 静态服务器最重要的安全点是防止访问根目录外文件。
- 不能简单字符串拼接后就打开文件，必须规范化和检查真实路径。

### 9.8 `mime.c` MIME 类型模块

该模块根据文件扩展名返回 HTTP `Content-Type`。

主要函数：

| 函数 | 作用 |
| --- | --- |
| `lume_mime_type_for_path` | 根据文件路径扩展名返回 MIME 类型 |

示例：

| 文件 | Content-Type |
| --- | --- |
| `.html` | `text/html; charset=utf-8` |
| `.css` | `text/css; charset=utf-8` |
| `.js` | `application/javascript; charset=utf-8` |
| `.png` | `image/png` |
| `.jpg` | `image/jpeg` |
| 未知扩展名 | `application/octet-stream` |

### 9.9 `buffer.c` 动态缓冲区模块

该模块用于保存响应头、错误响应体和发送偏移。

主要函数：

| 函数 | 作用 |
| --- | --- |
| `lume_buffer_init` | 初始化缓冲区 |
| `lume_buffer_free` | 释放缓冲区内存 |
| `lume_buffer_clear` | 清空缓冲区内容 |
| `lume_buffer_reserve` | 扩容缓冲区 |
| `lume_buffer_append` | 追加二进制数据 |
| `lume_buffer_append_str` | 追加字符串 |
| `lume_buffer_append_format` | 按格式追加字符串 |
| `lume_buffer_remaining` | 返回还没发送的数据长度 |
| `lume_buffer_current` | 返回当前发送位置指针 |
| `lume_buffer_advance` | 发送成功后推进偏移 |

答辩可讲重点：

- 网络写入可能一次写不完。
- buffer 中的 `offset` 用来记录已经发送到哪里。
- 这样可以正确处理 partial write。

### 9.10 `log.c` 日志模块

该模块用于输出运行信息和错误信息。

主要函数：

| 函数 | 作用 |
| --- | --- |
| `lume_log_set_level` | 设置日志级别 |
| `lume_log_message` | 输出带时间和级别的日志 |
| `level_name` | 将日志级别转成字符串 |

日志级别：

```text
DEBUG
INFO
WARN
ERROR
```

示例：

```text
[2026-07-14 10:00:00] INFO  listening on port 8080, root=/home/law/Lume/public
```

## 10. 测试模块说明

项目包含四个单元测试：

| 测试文件 | 测试内容 |
| --- | --- |
| `tests/test_config.c` | 配置解析、默认值、非法端口、非法连接数 |
| `tests/test_http.c` | HTTP 请求行解析和错误响应构造 |
| `tests/test_mime.c` | MIME 类型识别 |
| `tests/test_static_path.c` | 静态文件打开、404、目录遍历防护、符号链接逃逸防护 |

运行测试：

```bash
ctest --test-dir build --output-on-failure
```

端到端冒烟测试：

```bash
scripts/smoke.sh
```

冒烟测试验证：

- 首页访问成功。
- CSS/JS/图片访问成功。
- 大文件下载完整。
- 不存在文件返回 404。
- POST 返回 501。
- 目录遍历返回 400。
- 超长请求头返回 400。

## 11. 答辩时推荐讲解顺序

### 第一步：讲项目目标

可以这样说：

> 本项目实现了一个基于 Linux C 的轻量级静态 Web 服务器，主要目标是理解 HTTP、socket、epoll、非阻塞 I/O 和服务器模块化架构。它可以通过浏览器访问 HTML、CSS、JS、图片等静态资源。

### 第二步：讲总体架构

重点讲：

- `main` 是入口。
- `config` 读取配置。
- `server` 创建监听 socket。
- `event_loop` 使用 epoll 等待事件。
- `connection` 管理客户端连接。
- `http` 解析请求。
- `static_file` 查找文件。
- `mime` 设置类型。
- `buffer` 处理发送缓存。

### 第三步：讲一次请求流程

按这条线讲最清楚：

```text
浏览器 -> socket -> epoll -> connection -> http -> static_file -> mime -> response -> 浏览器
```

### 第四步：讲核心亮点

可以重点讲四个亮点：

1. 使用 epoll 实现事件驱动。
2. 使用非阻塞 I/O，避免单个慢连接卡住服务器。
3. 使用连接状态机处理读请求和写响应。
4. 使用路径规范化和 `realpath` 防止目录遍历攻击。

### 第五步：讲测试验证

说明项目不是只写了代码，还做了验证：

- 单元测试验证模块行为。
- 冒烟测试验证完整 HTTP 流程。
- `curl` 可以手动检查状态码。
- 浏览器可以直接访问页面。

## 12. 常见答辩问题和回答要点

### 问：为什么使用 epoll？

答：

epoll 是 Linux 下高效的 I/O 多路复用机制。它可以让一个线程同时管理多个连接，只有 fd 可读或可写时才处理，避免每个连接都创建线程，适合高并发网络服务器。

### 问：什么是 Reactor 模型？

答：

Reactor 是事件驱动模型。程序先把 fd 注册到事件循环中，然后等待事件发生。事件发生后，主循环根据事件类型分发给对应处理函数。本项目中 `event_loop.c` 就是 Reactor 的核心。

### 问：为什么 socket 要设置非阻塞？

答：

如果 socket 是阻塞的，一个慢客户端可能导致服务器卡在 `read` 或 `write` 上。设置非阻塞后，如果暂时不能读写，系统调用会返回 `EAGAIN`，服务器可以继续处理其他连接。

### 问：为什么要有连接状态机？

答：

因为一个连接不是一步完成的，它要经历读请求、处理请求、发送响应头、发送文件和关闭等阶段。非阻塞 I/O 下，一次读写可能不完整，所以必须保存状态和偏移。

### 问：如何防止目录遍历攻击？

答：

项目会先解码 URI，再拒绝 `..`、非法编码、空字节和控制字符，最后使用 `realpath` 获取真实路径，并检查真实路径必须仍在 `ROOT_DIR` 内。

### 问：为什么暂时不用 sendfile？

答：

当前阶段优先保证业务逻辑完整和连接处理正确，因此使用 `read/write` 分块发送文件。后续性能优化阶段可以引入 `sendfile` 减少用户态和内核态之间的数据拷贝。

### 问：服务器如何处理不存在的文件？

答：

`static_file.c` 使用 `realpath`、`stat` 和 `open` 查找文件。如果文件不存在或不是普通文件，会返回 `LUME_STATIC_NOT_FOUND`，最后由 HTTP 模块构造 `404 Not Found` 响应。

### 问：为什么 POST 返回 501？

答：

本项目定位是静态文件服务器，只支持 GET 获取资源。POST、PUT、DELETE 等动态请求方法不在当前实现范围内，所以返回 `501 Not Implemented`。

## 13. 项目当前不足和后续优化

当前不足：

- 不支持 HTTPS。
- 不支持 HTTP keep-alive。
- 不支持动态后端。
- 不支持 `sendfile`。
- 访问日志还比较简单。
- SPA history 路由刷新会 404。

后续优化方向：

- 使用 `sendfile` 提升文件发送性能。
- 增加访问日志，记录 URI、状态码、耗时和响应大小。
- 增加 keep-alive 支持。
- 增加 SPA fallback 配置。
- 增加更完整的压测报告。
- 增加更多异常请求测试。

## 14. 最简答辩总结

可以用下面这段作为结尾：

> Lume 项目实现了一个基于 Linux C、epoll 和非阻塞 I/O 的轻量级静态 Web 服务器。项目从配置加载、socket 监听、epoll 事件循环、连接状态机、HTTP 解析、静态文件映射、MIME 类型识别到测试脚本都进行了模块化设计。通过该项目，我理解了 Web 服务器从接收浏览器连接到返回静态资源的完整流程，也掌握了 Linux 网络编程、HTTP 协议基础、路径安全和 C 工程化开发方法。
