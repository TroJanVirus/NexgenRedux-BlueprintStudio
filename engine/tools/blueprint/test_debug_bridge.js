'use strict';
const assert=require('node:assert/strict'),net=require('node:net'),{spawn}=require('node:child_process'),B=require('./blueprint'),{createBridge}=require('./debug_bridge');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
async function deadline(p,ms,label){let timer;try{return await Promise.race([p,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error(label)),ms);})]);}finally{clearTimeout(timer);}}
async function freePort(){return await new Promise((resolve,reject)=>{const s=net.createServer();s.once('error',reject);s.listen(0,'127.0.0.1',()=>{const port=s.address().port;s.close(()=>resolve(port));});});}
async function main(){const fixture=process.argv[2];if(!fixture)throw Error('Pass the native nx_blueprint_debug_fixture executable');const port=await freePort();const bridge=createBridge({port,webPort:0});const a=await bridge.start();const url='http://127.0.0.1:'+a.port;const child=spawn(fixture,[String(port)],{stdio:['ignore','pipe','pipe']});let logs='';child.stdout.on('data',x=>logs+=x);child.stderr.on('data',x=>logs+=x);const exit=new Promise((resolve,reject)=>{child.once('error',reject);child.once('close',resolve);});
 async function post(body,token=bridge.token){const res=await fetch(url+'/debug',{method:'POST',headers:{Origin:url,'X-Nexgen-Token':token,'Content-Type':'application/json'},body:JSON.stringify(body)});return {status:res.status,body:await res.json()};}
 async function send(action,cmd,args){const r=await post({action,cmd,args});assert.equal(r.status,200,JSON.stringify(r.body));return r.body;}
 const ask=(cmd,args)=>send('command',cmd,args);
 async function stopped(){for(let i=0;i<80;i++){const r=await send('events');if(r.events.some(e=>e.event==='stopped'))return;await delay(30);}throw Error('No stop event: '+logs);}
 try{
  assert.equal((await post({action:'attach'},'wrong')).status,403);
  const page=await (await fetch(url)).text();assert.ok(page.includes(bridge.token));
  for(let i=0;;i++){const r=await post({action:'attach'});if(r.status===200)break;if(i===30)throw Error('Cannot attach: '+logs);await delay(50);}
  const hello=await ask('hello');assert.equal(hello.version,1);const doc=require('./examples/gameplay.bp.json');const result=B.traceCompile(doc);const map=B.debugMap(result.source);const before=map.find(x=>x.node==='broadcast'),after=map.find(x=>x.node==='unbind');assert.ok(before&&after);
  await ask('breakpoints',{file:'showcase/gameplay/main.as',lines:[before.line,after.line]});await ask('ready');await stopped();
  let stack=await ask('stack');assert.equal(stack.frames[0].line,before.line);assert.equal(stack.frames[0].file,'showcase/gameplay/main.as');let locals=await ask('variables',{frame:0,scope:'locals'});assert.ok(locals.variables.some(x=>x.name==='this'));let watch=await ask('evaluate',{expr:'this.total'});assert.equal(watch.result.value,'0');
  assert.equal((await post({action:'command',cmd:'profile',args:{on:'yes'}})).status,400);await ask('profile',{on:true});await ask('continue');await stopped();stack=await ask('stack');assert.equal(stack.frames[0].line,after.line);watch=await ask('evaluate',{expr:'this.total'});assert.equal(watch.result.value,'3');await ask('next');await stopped();stack=await ask('stack');assert.notEqual(stack.frames[0].line,after.line);
  const progress=map.find(x=>x.node==='progress');assert.ok(progress);await ask('breakpoints',{file:'showcase/gameplay/main.as',lines:[progress.line]});await ask('continue');await stopped();const profile=await ask('profile',{on:false});assert.ok(profile.samples>0,JSON.stringify(profile));assert.ok(profile.functions.length&&profile.lines.length,JSON.stringify(profile));assert.ok(profile.functions.some(f=>f.file==='showcase/gameplay/main.as'));await ask('breakpoints',{file:'showcase/gameplay/main.as',lines:[]});await ask('continue');assert.equal(await deadline(exit,4000,'Native debugger fixture did not close: '+logs),0,logs);
  // Leave a real, incomplete HTTP request open during shutdown. This must not
  // keep the debugger test or the local Studio process alive.
  const accepted=new Promise(resolve=>bridge.server.once('connection',resolve));
  const pending=net.createConnection({host:'127.0.0.1',port:a.port});
  const pendingClosed=new Promise(resolve=>pending.once('close',resolve));
  pending.on('error',()=>{});
  try{await deadline(accepted,4000,'HTTP connection was not accepted');pending.write('GET / HTTP/1.1\r\n');await deadline(bridge.close(),4000,'Bridge shutdown did not complete');await deadline(pendingClosed,4000,'HTTP connection was not closed');}finally{pending.destroy();}
 }finally{if(child.exitCode===null&&child.signalCode===null)child.kill();await deadline(bridge.close(),4000,'Bridge cleanup did not complete');}
 console.log('PASS native TCP attach, source map, exact node breakpoints, locals, watches, stepping, resume, native profiler commands, token protection, HTTP shutdown');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
