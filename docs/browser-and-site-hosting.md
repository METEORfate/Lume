# 浏览器访问与静态网站挂载指南

本文档面向新手，专门说明两件事：

1. 如何通过浏览器访问 Lume 静态 Web 服务器。
2. 如何把自己的 HTML、CSS、JavaScript、图片等静态网站放到服务器上运行。

Lume 当前是一个静态文件服务器。它不会执行 PHP、Python、Node.js 后端代码，也不会处理数据库请求。它的工作方式很直接：浏览器请求一个 URL，服务器就从配置的静态目录中找到对应文件并返回。

## 1. 先理解几个概念

### 服务器程序

编译后生成的服务器程序是：

```bash
build/lume_server
```

运行它以后，它会监听一个端口，等待浏览器访问。

### 配置文件

默认配置文件是：

```text
config/server.conf
```

当前默认内容类似：

```conf
PORT=8080
ROOT_DIR=public

MAX_REQUEST_BYTES=16384
MAX_CONNECTIONS=4096
REQUEST_TIMEOUT=30
```

最重要的是前两个配置：

| 配置项 | 含义 |
| --- | --- |
| `PORT` | 浏览器访问服务器时使用的端口 |
| `ROOT_DIR` | 服务器提供静态文件的根目录 |

### 静态文件根目录

默认静态文件根目录是：

```text
public/
```

也就是说，当浏览器访问：

```text
http://127.0.0.1:8080/index.html
```

服务器实际会去读取：

```text
public/index.html
```

当浏览器访问：

```text
http://127.0.0.1:8080/style.css
```

服务器实际会去读取：

```text
public/style.css
```

## 2. 编译服务器

第一次运行前，需要先编译项目。

在项目根目录执行：

```bash
cmake -S . -B build
cmake --build build
```

如果编译成功，会生成：

```text
build/lume_server
```

可以用下面命令确认文件存在：

```bash
ls build/lume_server
```

## 3. 启动服务器

在项目根目录执行：

```bash
./build/lume_server config/server.conf
```

如果启动成功，终端会输出类似日志：

```text
[2026-07-13 10:00:00] INFO  listening on port 8080, root=/home/law/Lume/public
```

看到这类日志后，不要关闭这个终端。服务器正在运行，浏览器才能访问它。

如果你按 `Ctrl+C`，服务器会停止，浏览器也就访问不到了。

## 4. 在本机浏览器访问

如果服务器和浏览器在同一台电脑上，打开浏览器，访问：

```text
http://127.0.0.1:8080/
```

或者：

```text
http://localhost:8080/
```

这两个地址都表示访问本机。

默认情况下，访问 `/` 会返回：

```text
public/index.html
```

所以浏览器打开首页时，本质上就是访问了 `public/index.html`。

## 5. 用 curl 验证访问是否正常

如果浏览器访问失败，可以先用 `curl` 检查服务器响应。

```bash
curl --noproxy '*' -i http://127.0.0.1:8080/
```

正常情况下会看到类似：

```http
HTTP/1.1 200 OK
Server: Lume-C
Content-Length: ...
Content-Type: text/html; charset=utf-8
Connection: close
```

如果看到：

```http
HTTP/1.1 404 Not Found
```

说明服务器运行了，但它在 `ROOT_DIR` 里没有找到对应文件。

如果连接失败，通常说明服务器没有启动、端口不对，或者端口被防火墙拦截。

## 6. 访问不同静态资源

假设 `public/` 目录中有这些文件：

```text
public/
├── index.html
├── style.css
├── app.js
└── images/
    └── logo.png
```

浏览器访问路径和本地文件对应关系如下：

| 浏览器 URL | 实际读取的文件 |
| --- | --- |
| `http://127.0.0.1:8080/` | `public/index.html` |
| `http://127.0.0.1:8080/index.html` | `public/index.html` |
| `http://127.0.0.1:8080/style.css` | `public/style.css` |
| `http://127.0.0.1:8080/app.js` | `public/app.js` |
| `http://127.0.0.1:8080/images/logo.png` | `public/images/logo.png` |

注意：URL 路径是相对于 `ROOT_DIR` 的，不是相对于整个项目根目录。

## 7. 挂载自己的静态网站：方式一，直接替换 public

这是最简单的方式。

假设你有一个自己的静态网站：

```text
my-site/
├── index.html
├── css/
│   └── main.css
├── js/
│   └── main.js
└── images/
    └── banner.jpg
```

你可以把这些文件复制到项目的 `public/` 目录中：

```bash
cp -r my-site/* public/
```

然后启动服务器：

```bash
./build/lume_server config/server.conf
```

浏览器访问：

```text
http://127.0.0.1:8080/
```

服务器会返回：

```text
public/index.html
```

这就是你的静态网站首页。

## 8. 挂载自己的静态网站：方式二，修改 ROOT_DIR

如果你不想把网站文件复制到 `public/`，可以直接修改配置文件，让服务器指向你的网站目录。

例如你的网站在：

```text
/home/law/my-site
```

目录结构是：

```text
/home/law/my-site/
├── index.html
├── css/main.css
└── js/main.js
```

修改 `config/server.conf`：

```conf
PORT=8080
ROOT_DIR=/home/law/my-site

MAX_REQUEST_BYTES=16384
MAX_CONNECTIONS=4096
REQUEST_TIMEOUT=30
```

然后重新启动服务器：

```bash
./build/lume_server config/server.conf
```

浏览器访问：

```text
http://127.0.0.1:8080/
```

这时服务器读取的是：

```text
/home/law/my-site/index.html
```

注意：修改配置后必须重启服务器，配置不会自动热更新。

## 9. 使用前端构建产物

如果你使用 Vue、React、Vite、Webpack 等前端工具，通常需要先执行构建命令。

例如 Vite 项目：

```bash
npm run build
```

构建后通常会生成：

```text
dist/
```

这个 `dist/` 才是可以直接挂载的静态网站目录。

你可以把配置改成：

```conf
PORT=8080
ROOT_DIR=/home/law/my-vite-project/dist

MAX_REQUEST_BYTES=16384
MAX_CONNECTIONS=4096
REQUEST_TIMEOUT=30
```

然后启动服务器：

```bash
./build/lume_server config/server.conf
```

浏览器访问：

```text
http://127.0.0.1:8080/
```

## 10. HTML 中资源路径怎么写

这是新手最容易遇到的问题。

推荐在 `index.html` 中使用以 `/` 开头的绝对路径：

```html
<link rel="stylesheet" href="/css/main.css">
<script src="/js/main.js"></script>
<img src="/images/logo.png" alt="logo">
```

这表示：

```text
/css/main.css      -> ROOT_DIR/css/main.css
/js/main.js        -> ROOT_DIR/js/main.js
/images/logo.png   -> ROOT_DIR/images/logo.png
```

也可以使用相对路径：

```html
<link rel="stylesheet" href="./css/main.css">
<script src="./js/main.js"></script>
```

但相对路径会受到当前页面路径影响。新手阶段建议优先使用 `/` 开头的路径。

## 11. 多页面网站怎么访问

假设目录是：

```text
public/
├── index.html
├── about.html
└── pages/
    └── contact.html
```

访问方式：

```text
http://127.0.0.1:8080/
http://127.0.0.1:8080/about.html
http://127.0.0.1:8080/pages/contact.html
```

当前服务器不支持目录列表。也就是说，如果目录里没有对应的 `index.html`，访问目录路径可能会得到 `404 Not Found`。

## 12. 单页应用路由注意事项

如果你使用 React Router、Vue Router 这类前端路由，并且使用 history 模式，可能会遇到这个问题：

```text
http://127.0.0.1:8080/about
```

刷新后返回：

```text
404 Not Found
```

原因是当前服务器会把 `/about` 当作真实文件路径，尝试读取：

```text
ROOT_DIR/about
```

但单页应用通常希望所有路径都返回 `index.html`，再由前端 JavaScript 接管路由。

当前 Lume 还没有实现 SPA fallback。如果要部署这种网站，可以先使用 hash 路由：

```text
http://127.0.0.1:8080/#/about
```

或者后续给服务器增加“找不到文件时返回 `index.html`”的可配置功能。

## 13. 在局域网其他设备访问

如果你希望手机或另一台电脑访问这个服务器，需要知道服务器电脑的局域网 IP。

在 Linux 上查看 IP：

```bash
ip addr
```

常见局域网 IP 类似：

```text
192.168.1.23
```

如果服务器运行在这台机器上，并且端口是 `8080`，其他设备可以访问：

```text
http://192.168.1.23:8080/
```

注意事项：

- 服务器电脑和访问设备需要在同一个局域网。
- 防火墙需要允许访问 `8080` 端口。
- 如果你修改了 `PORT`，URL 里的端口也要一起改。

如果访问失败，可以先在服务器本机确认：

```bash
curl --noproxy '*' -i http://127.0.0.1:8080/
```

本机能访问但局域网不能访问时，通常是防火墙或网络隔离问题。

## 14. 修改端口

如果 `8080` 被占用，可以修改 `config/server.conf`：

```conf
PORT=9090
ROOT_DIR=public

MAX_REQUEST_BYTES=16384
MAX_CONNECTIONS=4096
REQUEST_TIMEOUT=30
```

重启服务器：

```bash
./build/lume_server config/server.conf
```

浏览器访问：

```text
http://127.0.0.1:9090/
```

## 15. 常见问题

### 浏览器显示无法访问此网站

可能原因：

- 服务器没有启动。
- 访问端口写错了。
- 服务器启动失败。
- 防火墙拦截。

排查方法：

```bash
./build/lume_server config/server.conf
```

然后另开一个终端：

```bash
curl --noproxy '*' -i http://127.0.0.1:8080/
```

### 页面打开了，但 CSS 或 JS 没生效

可能原因：

- HTML 中资源路径写错。
- CSS 或 JS 文件没有放到 `ROOT_DIR` 里面。
- 文件名大小写不一致。

Linux 文件名区分大小写：

```text
main.css
Main.css
```

这是两个不同文件。

可以在浏览器开发者工具的 Network 面板中查看哪个文件返回了 `404`。

### 返回 404 Not Found

说明服务器运行正常，但找不到文件。

检查：

- URL 路径是否正确。
- 文件是否真的存在于 `ROOT_DIR` 中。
- `ROOT_DIR` 配置是否指向了正确目录。
- 文件名大小写是否一致。

### 修改网站文件后需要重启服务器吗

不需要。

服务器每次请求都会重新读取文件。你修改 HTML、CSS、JS 后，刷新浏览器即可。

如果浏览器缓存导致看不到变化，可以强制刷新：

```text
Ctrl + F5
```

### 修改 server.conf 后需要重启吗

需要。

配置文件只在服务器启动时读取一次。

### 可以挂载后端项目吗

不可以。

当前 Lume 只提供静态文件服务。它可以托管：

- `.html`
- `.css`
- `.js`
- 图片
- 字体
- JSON 静态文件

它不能直接运行：

- PHP
- Python Flask/Django
- Node.js Express
- Java Spring Boot
- 数据库接口

## 16. 推荐练习

你可以按下面顺序练习：

1. 启动默认服务器，访问 `http://127.0.0.1:8080/`。
2. 修改 `public/index.html`，刷新浏览器观察变化。
3. 新增 `public/about.html`，访问 `/about.html`。
4. 新增 `public/images/logo.png`，在 HTML 中用 `<img>` 引用。
5. 创建一个自己的 `my-site/` 目录，并把 `ROOT_DIR` 改过去。
6. 修改 `PORT=9090`，用新端口访问。
7. 用浏览器开发者工具查看每个资源的 HTTP 状态码和 `Content-Type`。

完成这些练习后，你就能掌握如何用 Lume 托管自己的静态网站。
