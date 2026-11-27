'use strict';
const assert=require('node:assert/strict'),net=require('node:net'),{spawn}=require('node:child_process'),B=require('./blueprint'),{createBridge}=require('./debug_bridge');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
async function freePort(){return await new Promise((resolve,reject)=>{const s=net.createServer();s.once('error',reject);s.listen(0,'127.0.0.1',()=>{const port=s.address().port;s.close(()=>resolve(port));});});}
async function main(){const fixture=process.argv[2];if(!fixture)throw Error('Pass the native nx_blueprint_debug_fixture executable');const port=await freePort();const bridge=createBridge({port,webPort:0});const a=await bridge.start();const url='http://127.0.0.1:'+a.port;const child=spawn(fixture,[String(port)],{stdio:['ignore','pipe','pipe']});let logs='';child.stdout.on('data',x=>logs+=x);child.stderr.on('data',x=>logs+=x);const exit=new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',resolve);});
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
  await ask('continue');await stopped();stack=await ask('stack');assert.equal(stack.frames[0].line,after.line);watch=await ask('evaluate',{expr:'this.total'});assert.equal(watch.result.value,'3');await ask('next');await stopped();stack=await ask('stack');assert.notEqual(stack.frames[0].line,after.line);
  await ask('breakpoints',{file:'showcase/gameplay/main.as',lines:[]});await ask('continue');assert.equal(await Promise.race([exit,delay(4000).then(()=>-1)]),0,logs);console.log('PASS native TCP attach, source map, exact node breakpoints, locals, watches, stepping, resume, token protection');
 }finally{bridge.close();child.kill();}
}
main().catch(e=>{console.error(e);process.exitCode=1;});
