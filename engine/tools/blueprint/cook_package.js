'use strict';
// Package a snapshot using the existing Nexgen cook executable. No conversion code here.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),{spawn}=require('node:child_process'),P=require('./project');
const MAX_OUTPUT=128*1024*1024;
const mediaType=/\.(?:png|jpe?g|tga|bmp|gif|wav|ogg|mp3)$/i;
function asBytes(value){return typeof value==='string'?Buffer.from(value):Buffer.from(value);}
function validatePaths(entries){
 const paths=new Set(),folders=new Set();
 for(const e of entries){const name=e.path.toLowerCase();if(!P.safeAsset(e.path)||paths.has(name)||folders.has(name))throw Error('Unsafe or duplicate package path: '+e.path);paths.add(name);const parts=name.split('/');for(let i=1;i<parts.length;i++){const dir=parts.slice(0,i).join('/');if(paths.has(dir))throw Error('File and directory conflict: '+e.path);folders.add(dir);}}
 for(const name of paths)if(name.startsWith('media/')){if(name==='media/.cooked'||paths.has(name+'.new')||folders.has(name+'.new'))throw Error('Path conflicts with Nexgen Cook temporary files: '+name);}
 if(folders.has('media/.cooked'))throw Error('Path conflicts with Nexgen Cook marker: media/.cooked');
}
function run(executable,args,{timeout=120000,signal}={}){return new Promise((resolve,reject)=>{
 const child=spawn(executable,args,{stdio:['ignore','pipe','pipe'],windowsHide:true,shell:false});let stdout='',stderr='',failure=null;
 const stop=why=>{failure=why;child.kill('SIGKILL');};const timer=setTimeout(()=>stop('Nexgen Cook timed out'),timeout);const cancel=()=>stop('Nexgen Cook cancelled');signal?.addEventListener('abort',cancel,{once:true});if(signal?.aborted)cancel();const cleanup=()=>{clearTimeout(timer);signal?.removeEventListener('abort',cancel);};
 child.stdout.setEncoding('utf8');child.stderr.setEncoding('utf8');
 child.stdout.on('data',b=>{stdout+=b;if(stdout.length>4*1024*1024)stop('Nexgen Cook output exceeded the limit');});
 child.stderr.on('data',b=>{if(stderr.length<16384)stderr+=b.toString('utf8').slice(0,16384-stderr.length);});
 child.once('error',e=>{cleanup();reject(e);});child.once('close',(code,signal)=>{cleanup();if(failure)return reject(Error(failure));let records;try{records=stdout.trim().split('\n').filter(Boolean).map(s=>JSON.parse(s));}catch{return reject(Error('Invalid JSON from Nexgen Cook'+(stderr?': '+stderr:'')));}const total=records.at(-1);if(code!==0||total?.done!==true||total.failed!==0){const failed=records.filter(r=>r.what==='failed'||r.result==='failed').slice(0,8);return reject(Error('Nexgen Cook failed ('+(signal||code)+'): '+(records.length?JSON.stringify(failed.length?failed:records.slice(-4)).slice(0,3000):stderr||'No report')));}resolve(total);});
 });}
function readOutput(root,expected){const actual=new Map();let bytes=0;function visit(dir,relative=''){for(const d of fs.readdirSync(dir,{withFileTypes:true})){const p=relative?relative+'/'+d.name:d.name,f=path.join(dir,d.name);if(d.isDirectory()){visit(f,p);continue;}if(!d.isFile())throw Error('Unexpected file type from Nexgen Cook: '+p);if(p==='.cooked')continue;if(!expected.has(p)||actual.has(p))throw Error('Unexpected file from Nexgen Cook: '+p);const size=fs.statSync(f).size;bytes+=size;if(bytes>MAX_OUTPUT)throw Error('Cooked media exceeds 128 MiB');const data=fs.readFileSync(f);if(!mediaType.test(p)&&!data.equals(expected.get(p)))throw Error('Nexgen Cook changed a non-image/audio file: '+p);actual.set(p,data);}}
 visit(root);if(actual.size!==expected.size)throw Error('Nexgen Cook did not return every media file');return actual;}
function createCooker({executable=null,timeout=120000}={}){let running=false,closing=false,controller=null,finished=null;return {available:!!executable,async stop(){closing=true;controller?.abort();if(finished)await finished;},async pack(project,platform){if(!executable)throw Error('Start the local bridge with --cook /path/to/cook');if(!['desktop','xboxog'].includes(platform))throw Error('Unknown Nexgen Cook platform');if(closing)throw Error('Nexgen Cook is closing');if(running)throw Error('Nexgen Cook is already running');running=true;controller=new AbortController();let done;finished=new Promise(resolve=>{done=resolve;});let temp;try{
 const r=P.projectEntries(project);validatePaths(r.entries);temp=fs.mkdtempSync(path.join(os.tmpdir(),'nexgen-bp-cook-'));const input=path.join(temp,'input'),output=path.join(temp,'output'),expected=new Map();fs.mkdirSync(input);
 for(const e of r.entries){if(!e.path.startsWith('media/'))continue;const name=e.path.slice(6),data=asBytes(e.data),file=path.join(input,...name.split('/'));expected.set(name,data);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,data,{flag:'wx'});}
 // The native model importer can read external paths. Keep models unchanged in this integration.
 const summary=await run(executable,['--platform',platform,'--skip','models','--json',input,output],{timeout,signal:controller.signal});const cooked=readOutput(output,expected);
 const entries=r.entries.map(e=>e.path.startsWith('media/')?{path:e.path,data:cooked.get(e.path.slice(6))}:e);entries.push({path:'COOK.txt',data:'Prepared using Nexgen Cook ('+platform+'). Images and sounds cooked; models left unchanged. Original project files were not changed.\n'});return {...r,entries,summary,bytes:P.zip(entries)};
 }finally{try{if(temp)fs.rmSync(temp,{recursive:true,force:true});}finally{running=false;controller=null;finished=null;done();}}}};}
module.exports={createCooker};
