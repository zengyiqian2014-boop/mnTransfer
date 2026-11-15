// The relay handles ciphertext only. No file storage and no pairing keys.
export const matchPath = path => /^\/v1\/([a-f0-9]{64})\/(send|receive)$/.exec(path);
export default {
  async fetch(request, env) {
    const path = new URL(request.url).pathname;
    if (path === '/health') return Response.json({service:'mnTransfer', version:1});
    const match = matchPath(path);
    if (!match || request.headers.get('Upgrade')?.toLowerCase() !== 'websocket')
      return new Response('WebSocket pairing endpoint required', {status:400});
    return env.ROOMS.get(env.ROOMS.idFromName(match[1])).fetch(request);
  }
};
export class Room {
  constructor(ctx) { this.ctx = ctx; }
  async fetch(request) {
    const match = matchPath(new URL(request.url).pathname);
    if (!match) return new Response('Bad room', {status:400});
    const role = match[2];
    if (this.ctx.getWebSockets(role).some(s=>s.readyState===1))
      return new Response('Role already connected', {status:409});
    const pair = new WebSocketPair(); const [client, socket] = Object.values(pair);
    this.ctx.acceptWebSocket(socket, [role]);
    socket.serializeAttachment({role, started:Date.now()});
    if (!await this.ctx.storage.getAlarm()) await this.ctx.storage.setAlarm(Date.now()+24*60*60*1000);
    socket.send('{"event":"waiting"}');
    const other = this.ctx.getWebSockets(role==='send'?'receive':'send').filter(s=>s.readyState===1);
    if (other.length) { socket.send('{"event":"paired"}'); other[0].send('{"event":"paired"}'); }
    return new Response(null, {status:101, webSocket:client});
  }
  webSocketMessage(socket, message) {
    const bytes = typeof message==='string' ? new TextEncoder().encode(message).length : message.byteLength;
    if (bytes > (typeof message==='string' ? 4096 : 131072)) {socket.close(1009,'Frame too large');return;}
    const {role, started} = socket.deserializeAttachment();
    if (Date.now()-started>24*60*60*1000) {socket.close(1008,'Room expired');return;}
    const peers = this.ctx.getWebSockets(role==='send'?'receive':'send');
    for (const peer of peers) if (peer.readyState===1) {
      try {peer.send(message);} catch {socket.close(1011,'Peer unavailable');}
    }
  }
  webSocketClose(socket, code, reason) {
    try {socket.close(code===1005?1000:code,reason);} catch {}
    const {role} = socket.deserializeAttachment();
    for (const peer of this.ctx.getWebSockets(role==='send'?'receive':'send'))
      try {peer.close(1001,'Peer disconnected');} catch {}
  }
  webSocketError(socket) {try {socket.close(1011,'Relay transport error');} catch {}}
  async alarm() {
    for (const socket of this.ctx.getWebSockets()) try {socket.close(1001,'Room expired');} catch {}
    await this.ctx.storage.deleteAll();
  }
}
