'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),{EventEmitter}=require('node:events'),{PassThrough}=require('node:stream'),{createRunner}=require('./playtest'),{createBridge}=require('./debug_bridge');
const root=fs.mkdtempSync(path.join(os.tmpdir(),'nexgen-playtest-')),exe=path.join(root,'nexgen'),showcase=path.join(root,'media','showcase');fs.mkdirSync(showcase,{recursive:true});fs.writeFileSync(exe,'test fixture');
const original=path.join(showcase,'existing');fs.mkdirSync(original);fs.writeFileSync(path.join(original,'main.as'),'keep me');
const project={version:1,kind:'BlueprintProject',name:'Test game',screens:[{id:'test_game',title:'Test game',blueprint:JSON.parse(JSON.stringify(require('./examples/arcade.bp.json'))),assets:[{path:'data/main.as',data:'YQ=='}]}]};
let args,opts,child;function fakeSpawn(file,a,o){assert.equal(file,fs.realpathSync(exe));args=a;opts=o;child=new EventEmitter();child.stdout=new PassThrough();child.stderr=new PassThrough();child.kill=signal=>{setImmediate(()=>child.emit('close',0,signal||null));return true;};return child;}
const runner=createRunner({engine:exe,spawnProcess:fakeSpawn});
async function main(){try{
 const duplicate=structuredClone(project);duplicate.screens[0].id='existing';assert.throws(()=>runner.start(duplicate,'existing'),/already exists/);assert.equal(fs.readFileSync(path.join(original,'main.as'),'utf8'),'keep me');assert.equal(runner.status().running,false);
 assert.throws(()=>runner.start(project,'nonexistent'),/Choose a screen/);const state=runner.start(project,'test_game');assert.ok(state.running);assert.equal(opts.shell,false);assert.deepEqual(args.slice(0,3),['--screen','test_game','--dev']);assert.equal(args[3],'--saves');assert.ok(fs.existsSync(args[4]));assert.ok(!args[4].startsWith(root));const script=path.join(showcase,'test_game','main.as');const initial=fs.readFileSync(script,'utf8');assert.ok(initial.includes('BP_Arcade'));assert.equal(fs.readFileSync(path.join(showcase,'test_game','asset','data','main.as'),'utf8'),'a');
 assert.throws(()=>runner.start(project,'test_game'),/Stop/);child.stdout.write('game log');assert.ok(runner.status().logs.includes('game log'));
 assert.equal(runner.reload(project).changed,0);const updated=structuredClone(project);updated.screens[0].blueprint.graphs[0].nodes.find(n=>n.id==='announce').defaults.text='New message';assert.equal(runner.reload(updated).changed,1);assert.ok(fs.readFileSync(script,'utf8').includes('New message'));
 const changedAsset=structuredClone(updated);changedAsset.screens[0].assets[0].data='Yg==';assert.throws(()=>runner.reload(changedAsset),/Stop and start/);assert.equal(fs.readFileSync(path.join(showcase,'test_game','asset','data','main.as'),'utf8'),'a');
 const invalid=structuredClone(updated);invalid.screens[0].blueprint.graphs[0].nodes[1].kind='missing';assert.throws(()=>runner.reload(invalid));assert.ok(fs.readFileSync(script,'utf8').includes('New message'));
 await runner.stop();assert.ok(!fs.existsSync(args[4]));assert.ok(!fs.existsSync(path.join(showcase,'test_game')));assert.ok(fs.existsSync(original));
 // A real child process and the authenticated HTTP endpoint on systems that support executable scripts.
 if(process.platform!=='win32'){
  fs.writeFileSync(exe,'#!/usr/bin/env node\nconsole.log(JSON.stringify(process.argv.slice(2)));setInterval(()=>{},1000);\n',{mode:0o755});fs.chmodSync(exe,0o755);
  const b=createBridge({webPort:0,previewEngine:exe});const addr=await b.start(),url='http://127.0.0.1:'+addr.port;
  async function send(action,extra={},token=b.token){const r=await fetch(url+'/play',{method:'POST',headers:{Origin:url,'Content-Type':'application/json','X-Nexgen-Token':token},body:JSON.stringify({action,...extra})});return {status:r.status,value:await r.json()};}
  try{assert.equal((await send('status',{},'wrong')).status,403);assert.equal((await send('status')).value.configured,true);assert.equal((await send('start',{project,screen:'test_game'})).value.running,true);let s;for(let i=0;i<50;i++){s=(await send('status')).value;if(s.logs.includes('"--screen"'))break;await new Promise(r=>setTimeout(r,30));}assert.ok(s.logs.includes('"--screen"'));assert.equal((await send('reload',{project:updated})).value.changed,1);assert.equal((await send('stop')).value.running,false);assert.ok(!fs.existsSync(path.join(showcase,'test_game')));assert.equal(fs.readFileSync(path.join(original,'main.as'),'utf8'),'keep me');}finally{await b.close();}
 }
 console.log('PASS safe test staging, collision refusal, isolated saves, script reload, asset change refusal, cleanup'+(process.platform==='win32'?' (mock child)':' and authenticated real-process bridge'));
 }finally{await runner.stop();fs.rmSync(root,{recursive:true,force:true});}}
main().catch(e=>{console.error(e);process.exitCode=1;});
