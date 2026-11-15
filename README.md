# mnTransfer

C++ 文件传输客户端，提供 Windows、macOS、Linux 三个构建。发送方选文件，接收方粘贴配对码；通过公网 WSS 中继连接，不依赖 Tailscale，不需要配置路由器端口转发。

配对码同时包含中继地址和随机密钥。文件在客户端加密，中继只转发密文。传输使用 64 KiB 分块和流量控制，内存占用不随文件大小增长；支持断线重连、断点核对、SHA-256 完整校验。已有同名文件不会被覆盖。

## 下载和使用

[下载发布包](https://github.com/zengyiqian2014-boop/mnTransfer/releases)

1. Windows 解压 `mnTransfer-Windows-x64.zip`，双击 `mnTransfer.exe`。需要 Windows 10/11 x64。macOS 解压后打开 `.app`；Linux 解压后执行 `./run.sh`。
2. 发送方首次填写已部署中继的 `wss://` 地址，点击“选择文件并生成配对码”。把完整配对码私下发给接收方。
3. 接收方在“接收”页粘贴配对码，选择保存文件夹。核对文件名、大小后确认。
4. 两端保持打开，显示“传输完成，SHA-256 一致”才算成功。断线后会重连；退出程序后可用原发送会话的配对码继续，发送端须仍保留该会话。

**下载客户端不会自动部署中继。** 源码不内置一个虚构的公共服务器。中继部署完成后，发送方设置一次地址，接收方直接用配对码即可。[Cloudflare 部署步骤](relay/README.md)；也提供 C++ 中继供本地测试或 VPS 部署。

预览版尚未购买 Windows 代码签名或完成 Apple 公证。macOS 安装包的实际 CPU 架构写在 `BUILD_INFO.txt`。Linux 包在 Ubuntu 24.04 构建，仍依赖系统图形、OpenGL、字体和 C/C++ 运行库；不是适配所有发行版的静态程序。

## 命令行

客户端和中继使用同一份 C++ 程序，不需要 Python 或 Java。

```sh
# 发送端：配对码只写入新文件，避免混入日志
mnTransfer --send mnOS.iso --relay wss://transfer.example.com --code-file send.code

# 接收端：把接收到的配对码保存到 receive.code
mnTransfer --code-file receive.code --out "$HOME/Downloads"

# 仅限本机开发测试；公网客户端拒绝明文 ws:// 地址
mnTransfer --serve --port 8787
```

中断时 `.mntransfer-<SHA256>.part` 保留在接收文件夹。重连后双方重新核对已有前缀；不匹配会从头重传。完成前不改名为正式文件。相同文件的多个接收任务应使用不同目标文件夹。

## 构建和验证

需要 C++17、CMake 3.21+、Qt 6.4+（Core/Widgets/Network/WebSockets/Test）及 libsodium。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
node --test relay/test/*.test.mjs
```

GitHub Actions 对三个平台编译、测试后打包。发布标签只有在三端通过后才生成 Release。测试覆盖二进制和空文件、多块流控、正确/损坏断点、连接中断、文件名越界拒绝、同名文件保护、密文篡改和错误密钥。

加密采用 libsodium 的 XChaCha20-Poly1305 secretstream，未自行设计密码算法。连接挑战、方向密钥、流顺序和最终哈希的处理见 [PROTOCOL.md](PROTOCOL.md)。这是新项目，尚未经过独立安全审计。配对码持有者可以接收文件；中继仍能观察 IP、时间和流量大小，也能拒绝服务。

网络传输速度取决于两端连接和中继带宽；Cloudflare 公网服务并不保证中国大陆访问速度。可以部署在自己实际可用的域名/网络上。

反馈：report.mnos@proton.me、wrmfwv@163.com。软件按 GPL-3.0-or-later 发布，**不提供任何担保（NO WARRANTY）**。
