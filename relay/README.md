# 部署公网中继

这是 WebSocket 中继，不是静态网页，不能只把文件拖进 Cloudflare Pages。
服务器只转发加密数据，不存 ISO，也不接收配对码中的密钥。两台客户端同时在线。

## Cloudflare Workers

账号需要能使用 Workers 和 SQLite Durable Objects。确认账户的流量/用量额度。

1. 下载 Release 里的 `mnTransfer-Cloudflare-relay.zip` 并解压，或者在源码中进入 `relay` 目录。
2. 安装 Node.js 22 LTS。它只用于部署中继；Windows/macOS/Linux 客户端不需要 Node.js。
3. 在这个目录运行：

   ```sh
   npm ci
   npx wrangler login
   npm test
   npm run deploy
   ```

4. 浏览器登录 Cloudflare 并授权部署。终端会显示实际的 `https://mntransfer-relay.<账号>.workers.dev` 地址。
5. 发送客户端填写相应的 `wss://mntransfer-relay.<账号>.workers.dev`。接收方只需要完整配对码。
6. 如使用自己的域名，在 Worker 的 **Settings → Domains & Routes → Add → Custom Domain** 添加，例如 `transfer.micronull.cn`。客户端改用 `wss://transfer.micronull.cn`。无需跳转到 GitHub。

验证 `https://你的域名/health` 返回 `{"service":"mnTransfer","version":1}`，然后先传一个小文件，再传 ISO。部署成功的网页提示不能替代真实传输测试。

## GitHub 自动部署

仓库提供 `Deploy Cloudflare public relay` 工作流。创建 `relay` Environment，添加：

- `CLOUDFLARE_API_TOKEN`：仅授予目标账户部署这个 Worker / Durable Object 所需权限。
- `CLOUDFLARE_ACCOUNT_ID`：目标 Cloudflare 账户 ID。

在 GitHub 的 Secrets 页面设置，**不要写进源码、README、聊天或配对码**。随后手动运行工作流。
没有配置这些凭据时，不会自动向任何 Cloudflare 账号发布。

## 自己的服务器

运行 C++ 客户端的 `mnTransfer --serve --port 8787`，用支持 WebSocket 的
Caddy / Nginx 等把有效 HTTPS 域名反向代理到 `127.0.0.1:8787`。
客户端始终使用 `wss://`。中继房间最长 24 小时；一对端点只允许一个发送者和一个接收者。
应在反向代理/Cloudflare 上设置适合自己带宽和预算的连接及访问限制。
