import {test} from 'node:test';
import assert from 'node:assert/strict';
import worker, {Room,matchPath} from '../src/worker.mjs';
test('only opaque 256-bit room IDs and the two roles are routed',()=>{
  assert.ok(matchPath('/v1/'+'a'.repeat(64)+'/send'));
  for(const s of ['/v1/short/send','/v1/'+'a'.repeat(64)+'/admin','/v1/'+'A'.repeat(64)+'/receive','/v1/../send']) assert.equal(matchPath(s),null);
});
test('health and non-WebSocket requests do not allocate rooms',async()=>{
  const noRooms={ROOMS:{get(){throw Error('Unexpected allocation');}}};
  assert.equal((await worker.fetch(new Request('https://relay/health'),noRooms)).status,200);
  assert.equal((await worker.fetch(new Request('https://relay/v1/'+'a'.repeat(64)+'/send'),noRooms)).status,400);
});
test('ciphertext is forwarded only to the opposite role; oversized data is rejected',()=>{
  const sent=[],closed=[];const peer={readyState:1,send:b=>sent.push(b)};
  const room=new Room({getWebSockets:role=>role==='receive'?[peer]:[]});
  const socket={deserializeAttachment:()=>({role:'send',started:Date.now()}),close:(...args)=>closed.push(args)};
  const cipher=new Uint8Array([0,255,27,9]).buffer;room.webSocketMessage(socket,cipher);
  assert.equal(sent.length,1);assert.equal(sent[0],cipher);
  room.webSocketMessage(socket,new ArrayBuffer(131073));assert.equal(closed[0][0],1009);assert.equal(sent.length,1);
});
test('expired sessions stop forwarding',()=>{
  let closed=false;const room=new Room({getWebSockets(){throw Error('expired room forwarded');}});
  room.webSocketMessage({deserializeAttachment:()=>({role:'send',started:0}),close:()=>{closed=true;}},'x');assert.ok(closed);
});
