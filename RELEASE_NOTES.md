Windows、macOS、Linux 文件传输预览版。C++ 客户端，公网配对，客户端加密，断点核对与重连，SHA-256 完整校验，同名文件不覆盖。

Windows 下载 `mnTransfer-Windows-x64.zip`，解压运行 `mnTransfer.exe`。接收端粘贴发送方的完整配对码即可。下载客户端不等于已经部署了公网中继；仓库附 Cloudflare Workers 中继源码、部署包和步骤。需要一条真实可用的 `wss://` 中继地址，两端都要在线。

配对码是接收凭据，请只分享给接收者。预览版未做独立安全审计、Windows 代码签名或 Apple 公证。不保证所有地区的网络速度。实际平台架构和依赖版本见各包 `BUILD_INFO.txt`，SHA-256 见 `SHA256SUMS`。软件不提供任何担保。
