# Lume Static Server

Lume 是一个基于 Linux C 的轻量级静态 Web 服务器。它使用 `epoll`
实现单线程 Reactor 事件循环，支持 HTTP GET 请求，并可以把配置目录下的
HTML、CSS、JavaScript、图片等静态文件返回给浏览器或 `curl`。

这个项目的第一阶段目标不是追求极限性能，而是先实现一个完整、严谨、易读、
可维护的服务器基础版本。后续可以在这个基础上继续加入 `sendfile`、边缘触发
`epoll`、连接超时、更多配置项等性能增强。

## 1. 项目能做什么

当前版本支持：

- 监听指定端口，接受 TCP 客户端连接。
- 使用 `epoll` 管理监听 socket 和客户端 socket。
- 解析 HTTP/1.0、HTTP/1.1 请求行。
- 支持 `GET` 方法。
- 非 `GET` 方法返回 `501 Not Implemented`。
- 请求格式错误返回 `400 Bad Request`。
- 静态文件不存在返回 `404 Not Found`。
- 正常静态文件返回 `200 OK`。
- 根据文件扩展名设置 `Content-Type`。
- 通过路径规范化和 `realpath` 防御目录遍历攻击。
- 使用短连接模型，请求响应结束后主动关闭连接。

当前版本暂不支持：

- HTTPS/TLS。
- HTTP keep-alive 长连接。
- 目录列表。
- 动态接口、CGI、反向代理。
- HTTP 缓存协商，例如 `ETag`、`If-Modified-Since`。
- `sendfile` 零拷贝优化。

## 2. 目录结构

```text
.
├── CMakeLists.txt              # CMake 构建入口
├── README.md                   # 项目说明文档
├── config/
│   └── server.conf             # 默认配置文件
├── docs/
│   └── architecture.md         # 简短架构说明
├── include/lume/               # 对外头文件
│   ├── buffer.h                # 动态缓冲区
│   ├── config.h                # 配置加载
│   ├── connection.h            # 客户端连接生命周期
│   ├── event_loop.h            # epoll 事件循环
│   ├── http.h                  # HTTP 解析和响应构造
│   ├── log.h                   # 日志
│   ├── mime.h                  # MIME 类型识别
│   ├── server.h                # 服务器启动和 socket 初始化
│   └── static_file.h           # 静态文件映射和安全检查
├── public/                     # 默认静态文件根目录
│   ├── app.js
│   ├── index.html
│   └── style.css
├── scripts/
│   ├── bench.sh                # 简单压测入口
│   └── smoke.sh                # 端到端冒烟测试
├── src/                        # 具体实现
│   ├── buffer.c
│   ├── config.c
│   ├── connection.c
│   ├── event_loop.c
│   ├── http.c
│   ├── log.c
│   ├── main.c
│   ├── mime.c
│   ├── server.c
│   └── static_file.c
└── tests/                      # 单元测试
    ├── test_config.c
    ├── test_http.c
    ├── test_mime.c
    └── test_static_path.c
```

建议新手按这个顺序阅读源码：

1. `src/main.c`
2. `src/config.c`
3. `src/server.c`
4. `src/event_loop.c`
5. `src/connection.c`
6. `src/http.c`
7. `src/static_file.c`
8. `src/mime.c`
9. `src/buffer.c`

## 3. 整体架构

服务器采用单线程 Reactor 模型：

```text
main
  |
  v
load config
  |
  v
create listen socket
  |
  v
epoll_wait loop
  |
  +-- listener readable -> accept new clients
  |
  +-- client readable   -> read HTTP request
  |
  +-- build response    -> parse HTTP + open static file
  |
  +-- client writable   -> write headers + file content
  |
  v
close connection
```

### 为什么使用 epoll

传统阻塞模型中，如果一个客户端连接很慢，服务器线程可能会卡在 `read` 或
`write` 上。`epoll` 的思路是：把多个 fd 注册给内核，然后由内核告诉程序
“哪些 fd 现在可以读或可以写”。这样单个线程就可以管理许多连接。

当前项目使用水平触发模式，也就是默认的 `EPOLLLT`。这种方式更容易写对，
适合作为第一版可靠实现。后续性能阶段可以再评估 `EPOLLET` 边缘触发。

## 4. 核心模块讲解

### `main`

位置：[src/main.c](src/main.c)

职责：

- 读取命令行参数中的配置文件路径。
- 默认使用 `config/server.conf`。
- 忽略 `SIGPIPE`，避免客户端提前断开时服务器被信号杀死。
- 加载配置。
- 初始化服务器。
- 启动事件循环。

运行入口大致是：

```c
lume_config_load(...);
lume_server_init(...);
lume_server_run(...);
```

### `config`

位置：[src/config.c](src/config.c)

职责：

- 解析配置文件。
- 支持空行和 `#` 注释。
- 支持 `PORT` 和 `ROOT_DIR`。
- 校验端口范围必须是 `1` 到 `65535`。
- 使用 `realpath` 把 `ROOT_DIR` 转成真实绝对路径。
- 检查 `ROOT_DIR` 必须是存在的目录。

默认配置：

```conf
PORT=8080
ROOT_DIR=public
```

注意：`ROOT_DIR` 必须已经存在。如果配置成不存在的目录，服务器会启动失败。

### `server`

位置：[src/server.c](src/server.c)

职责：

- 创建 TCP socket。
- 设置 `SO_REUSEADDR`，方便开发时快速重启服务。
- 设置非阻塞 socket。
- 绑定 `0.0.0.0:PORT`。
- 调用 `listen` 开始监听。

主要系统调用：

```text
socket
setsockopt
bind
listen
fcntl
close
```

### `event_loop`

位置：[src/event_loop.c](src/event_loop.c)

职责：

- 创建 epoll 实例。
- 把监听 fd 注册到 epoll。
- 调用 `epoll_wait` 等待事件。
- 监听 fd 可读时，循环 `accept4` 新连接。
- 客户端 fd 有事件时，转发给 `connection` 模块处理。
- 连接完成或出错后销毁连接并关闭 fd。

主要系统调用：

```text
epoll_create1
epoll_ctl
epoll_wait
accept4
```

### `connection`

位置：[src/connection.c](src/connection.c)

这是项目中最重要的业务模块。它管理单个客户端连接的完整生命周期。

当前状态包括：

```text
CONNECTION_READING   -> 正在读取请求
CONNECTION_WRITING   -> 正在发送响应
CONNECTION_CLOSING   -> 准备关闭连接
```

读取流程：

1. 客户端 fd 触发 `EPOLLIN`。
2. `connection` 循环 `read` 数据。
3. 数据追加到读缓冲区。
4. 检查是否已经读到 HTTP 头结束标记。
5. 请求完整后调用 HTTP 解析。
6. 根据解析结果准备错误响应或静态文件响应。

写入流程：

1. 客户端 fd 触发 `EPOLLOUT`。
2. 先发送响应头缓冲区。
3. 如果有文件，再分块读取文件并写到 socket。
4. 响应发送完毕后关闭连接。

当前版本为了业务正确性，使用 `read + write` 分块发送文件。后续性能优化阶段
可以把文件发送替换成 `sendfile`。

### `http`

位置：[src/http.c](src/http.c)

职责：

- 解析 HTTP 请求行。
- 提取请求方法、URI、HTTP 版本。
- 判断请求是否完整、是否非法。
- 构造标准 HTTP 响应头。
- 构造错误响应页面。

支持的请求行示例：

```http
GET /index.html HTTP/1.1
```

返回状态码：

```text
200 OK
400 Bad Request
404 Not Found
501 Not Implemented
```

响应头示例：

```http
HTTP/1.1 200 OK
Server: Lume-C
Content-Length: 123
Content-Type: text/html; charset=utf-8
Connection: close
```

### `static_file`

位置：[src/static_file.c](src/static_file.c)

职责：

- 将 URI 映射到 `ROOT_DIR` 下的真实文件。
- `/` 默认映射为 `/index.html`。
- 解码 URL 中的 `%xx` 编码。
- 拒绝非法路径。
- 使用 `realpath` 确认最终文件仍在文档根目录内。
- 使用 `stat` 获取文件大小。
- 使用 `open` 打开静态文件。

安全防护包括：

- 拒绝 `..` 路径段。
- 拒绝非法 `%xx` 编码。
- 拒绝空字节。
- 拒绝控制字符。
- 拒绝反斜杠 `\`。
- 拒绝通过符号链接逃逸文档根目录。

例如这些请求不能访问系统文件：

```text
/../etc/passwd
/%2e%2e/etc/passwd
```

### `mime`

位置：[src/mime.c](src/mime.c)

职责：

- 根据文件扩展名返回 `Content-Type`。

例如：

```text
.html -> text/html; charset=utf-8
.css  -> text/css; charset=utf-8
.js   -> application/javascript; charset=utf-8
.png  -> image/png
.jpg  -> image/jpeg
```

未知扩展名返回：

```text
application/octet-stream
```

### `buffer`

位置：[src/buffer.c](src/buffer.c)

职责：

- 提供简单动态缓冲区。
- 支持追加字符串、追加二进制数据、格式化追加。
- 记录当前发送偏移，方便处理 partial write。

虽然第一阶段没有追求极限性能，但这个模块让响应头构造和发送逻辑更清晰。

### `log`

位置：[src/log.c](src/log.c)

职责：

- 输出带时间和级别的日志。
- 支持 `DEBUG`、`INFO`、`WARN`、`ERROR`。

示例：

```text
[2026-07-09 16:36:29] ERROR socket failed: Operation not permitted
```

## 5. 编译项目

需要环境：

- Linux。
- CMake 3.16 或更高版本。
- GCC 或 Clang。
- `make` 或 CMake 支持的其他构建工具。

在项目根目录执行：

```bash
cmake -S . -B build
cmake --build build
```

编译完成后会生成：

```text
build/lume_server
build/test_config
build/test_http
build/test_mime
build/test_static_path
```

如果你想重新干净构建：

```bash
rm -rf build
cmake -S . -B build
cmake --build build
```

## 6. 运行服务器

默认运行：

```bash
./build/lume_server config/server.conf
```

如果不传配置文件参数，也会默认读取：

```bash
config/server.conf
```

成功启动后会看到类似日志：

```text
[2026-07-09 16:40:00] INFO  listening on port 8080, root=/home/law/Lume/public
```

然后打开浏览器访问：

```text
http://127.0.0.1:8080/
```

或者用 `curl`：

```bash
curl --noproxy '*' -i http://127.0.0.1:8080/
curl --noproxy '*' -i http://127.0.0.1:8080/style.css
curl --noproxy '*' -i http://127.0.0.1:8080/app.js
```

如果你的环境没有代理，可以省略 `--noproxy '*'`。这个项目的冒烟测试里加它，
是为了避免本机代理环境变量影响 localhost 测试。

## 7. 修改配置

配置文件位置：

```text
config/server.conf
```

内容示例：

```conf
# Lume static server configuration.
PORT=8080
ROOT_DIR=public
```

字段说明：

| 字段 | 作用 | 示例 |
| --- | --- | --- |
| `PORT` | 服务器监听端口 | `8080` |
| `ROOT_DIR` | 静态文件根目录 | `public` |

例如你想监听 `9090`：

```conf
PORT=9090
ROOT_DIR=public
```

例如你想把网站文件放到 `/var/www/demo`：

```conf
PORT=8080
ROOT_DIR=/var/www/demo
```

注意：

- `ROOT_DIR` 必须存在。
- 服务启动时会把 `ROOT_DIR` 转成真实绝对路径。
- URI 只能访问 `ROOT_DIR` 内部文件。

## 8. 测试项目

### 8.1 运行单元测试

先编译：

```bash
cmake -S . -B build
cmake --build build
```

再运行：

```bash
ctest --test-dir build --output-on-failure
```

当前单元测试包括：

| 测试 | 覆盖内容 |
| --- | --- |
| `test_config` | 配置文件解析、端口校验、根目录解析 |
| `test_http` | HTTP 请求行解析、错误响应构造 |
| `test_mime` | 文件扩展名到 MIME 类型映射 |
| `test_static_path` | 静态文件打开、404、目录遍历防护、符号链接逃逸防护 |

### 8.2 运行端到端冒烟测试

```bash
scripts/smoke.sh
```

这个脚本会：

1. 创建临时静态目录。
2. 生成临时配置文件。
3. 启动 `build/lume_server`。
4. 使用 `curl` 请求 HTML、CSS、JS、图片。
5. 验证 `404`。
6. 验证 `POST` 返回 `501`。
7. 验证目录遍历请求返回 `400`。
8. 自动关闭服务器并清理临时目录。

默认测试端口是 `18080`。如果你想换端口：

```bash
LUME_SMOKE_PORT=18081 scripts/smoke.sh
```

### 8.3 简单压测

先启动服务器：

```bash
./build/lume_server config/server.conf
```

再开另一个终端执行：

```bash
scripts/bench.sh http://127.0.0.1:8080/
```

脚本优先使用 `wrk`，如果没有 `wrk`，会尝试使用 `ab`。

安装工具示例：

```bash
sudo apt install wrk apache2-utils
```

注意：当前第一阶段还没有 `sendfile` 和边缘触发优化，压测结果主要用作后续优化
前的基线，不代表最终性能目标。

## 9. 手动验证常见 HTTP 行为

启动服务器后，可以用这些命令理解服务器行为。

### 正常首页

```bash
curl --noproxy '*' -i http://127.0.0.1:8080/
```

预期：

```text
HTTP/1.1 200 OK
```

### 静态 CSS

```bash
curl --noproxy '*' -i http://127.0.0.1:8080/style.css
```

预期包含：

```text
Content-Type: text/css; charset=utf-8
```

### 不存在的文件

```bash
curl --noproxy '*' -i http://127.0.0.1:8080/not-found.html
```

预期：

```text
HTTP/1.1 404 Not Found
```

### 不支持的方法

```bash
curl --noproxy '*' -i -X POST http://127.0.0.1:8080/
```

预期：

```text
HTTP/1.1 501 Not Implemented
```

### 目录遍历攻击

```bash
curl --noproxy '*' -i http://127.0.0.1:8080/%2e%2e/etc/passwd
```

预期：

```text
HTTP/1.1 400 Bad Request
```

## 10. 一次请求在代码里如何流动

以访问首页为例：

```bash
curl --noproxy '*' -i http://127.0.0.1:8080/
```

代码流程：

1. `main.c`
   - 加载 `config/server.conf`。
   - 初始化服务器。
   - 进入事件循环。

2. `server.c`
   - 创建监听 socket。
   - 绑定端口。
   - 开始监听。

3. `event_loop.c`
   - `epoll_wait` 等待事件。
   - 发现监听 fd 可读。
   - 调用 `accept4` 接受客户端连接。
   - 把客户端 fd 注册进 epoll。

4. `connection.c`
   - 发现客户端 fd 可读。
   - 调用 `read` 读取 HTTP 请求。
   - 检查是否读到 `\r\n\r\n`。
   - 请求完整后进入响应构造。

5. `http.c`
   - 解析 `GET / HTTP/1.1`。
   - 判断方法是 `GET`。

6. `static_file.c`
   - 把 `/` 映射为 `index.html`。
   - 拼接到 `ROOT_DIR` 下。
   - 使用 `realpath` 检查路径安全。
   - 使用 `stat` 取得文件大小。
   - 使用 `open` 打开文件。

7. `mime.c`
   - 根据 `.html` 返回 `text/html; charset=utf-8`。

8. `http.c`
   - 构造响应头。

9. `connection.c`
   - 先发送响应头。
   - 再分块读取并发送文件内容。
   - 发送完成后关闭连接。

## 11. 常见问题

### 端口被占用

现象：

```text
bind port 8080 failed: Address already in use
```

解决：

- 修改 `config/server.conf` 中的 `PORT`。
- 或关闭占用该端口的进程。

查看端口占用：

```bash
ss -ltnp | grep 8080
```

### 访问不到服务器

检查：

1. 是否已经编译：

   ```bash
   ls build/lume_server
   ```

2. 是否已经启动：

   ```bash
   ./build/lume_server config/server.conf
   ```

3. 端口是否和配置一致：

   ```bash
   cat config/server.conf
   ```

4. 本机是否有代理环境变量影响 `curl`：

   ```bash
   curl --noproxy '*' -i http://127.0.0.1:8080/
   ```

### `ROOT_DIR` 报错

现象：

```text
failed to resolve ROOT_DIR
```

原因通常是配置的目录不存在。

解决：

```bash
mkdir -p public
```

或者把 `ROOT_DIR` 改成一个已经存在的目录。

### 修改静态文件后需要重启吗

不需要。服务器每次请求都会重新打开文件，所以修改 `public/index.html` 后刷新
浏览器即可看到变化。

### 修改配置后需要重启吗

需要。配置只在启动时读取一次。

## 12. 后续优化方向

阶段二建议：

- 完善更细的连接状态机，例如 `WRITING_HEADER`、`WRITING_FILE`。
- 增加连接空闲超时。
- 增加最大连接数限制。
- 更系统地处理 partial read 和 partial write 的边界情况。
- 增加更多异常请求测试。

阶段三建议：

- 使用 `sendfile` 发送静态文件，减少用户态拷贝。
- 评估并引入 `EPOLLET` 边缘触发。
- 增加配置项：`BACKLOG`、`MAX_EVENTS`、`MAX_CONNECTIONS`。
- 优化响应头构造和缓冲区复用。
- 用 `wrk` 持续压测并记录 QPS、延迟和错误率。

阶段四建议：

- 增加访问日志。
- 增加更完整的开发文档。
- 为 keep-alive、TLS、目录索引等功能预留接口。

## 13. 学习路线

如果你是新手，可以按下面路线掌握这个项目：

1. 先运行服务器，确认浏览器能访问首页。
2. 修改 `public/index.html`，刷新浏览器观察变化。
3. 修改 `PORT`，理解配置加载。
4. 用 `curl -i` 查看 HTTP 响应头。
5. 阅读 `src/main.c` 和 `src/server.c`，理解启动流程。
6. 阅读 `src/event_loop.c`，理解 `epoll_wait` 如何分发事件。
7. 阅读 `src/connection.c`，理解一次请求如何变成一次响应。
8. 阅读 `src/static_file.c`，重点理解路径安全检查。
9. 运行 `ctest`，再试着给 HTTP 解析新增一个测试。
10. 最后再尝试实现一个小功能，例如增加 `.pdf` 的 MIME 类型。

这个项目的核心价值在于把 Linux 网络编程、HTTP 基础、文件 I/O、模块化 C
工程组织放在同一个小而完整的系统里。读懂它以后，再做高性能优化会踏实很多。
