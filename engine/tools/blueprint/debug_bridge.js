#!/usr/bin/env node
'use strict';
// Opt-in local HTTP to Nexgen's newline-delimited TCP debugger. No third-party packages.
const http=require('node:http'), net=require('node:net'), fs=require('node:fs'), path=require('node:path'), crypto=require('node:crypto'),{spawn}=require('node:child_process');
function createBridge({host='127.0.0.1',port=4711,webPort=8765,validator=null,previewEngine=null,cookExecutable=null}={}) {
    const preview=require('./playtest').createRunner({engine:previewEngine});
    const cooker=require('./cook_package').createCooker({executable:cookExecutable});
    const token=crypto.randomBytes(24).toString('hex');
    const html=fs.readFileSync(path.join(__dirname,'index.html'),'utf8')
        .replace('<link rel="stylesheet" href="studio.css">','<style>'+fs.readFileSync(path.join(__dirname,'studio.css'),'utf8')+'</style>')
        .replace(/<script src="([^"]+)"><\/script>/g,(_,name)=>'<script>'+fs.readFileSync(path.join(__dirname,name),'utf8').replace(/<\/script/gi,'<\\/script')+'</script>')
        .replace('<script>','<script>window.NEXGEN_DEBUG_TOKEN='+JSON.stringify(token)+';</script><script>');
    let socket=null, connected=false, seq=0, buffer='', events=[], waiting=new Map(),validating=false,lastContact=Date.now();
    function enqueue(value){events.push(value); if(events.length>500)events.shift();}
    function disconnect(){const old=socket;socket=null;connected=false;buffer='';if(old)old.destroy();for(const p of waiting.values()){clearTimeout(p.timer);p.reject(Error('Debugger disconnected'));}waiting.clear();}
    function attach(){if(socket)throw Error('Already connected or connecting');return new Promise((resolve,reject)=>{
        const s=net.createConnection({host,port});socket=s;s.setEncoding('utf8');s.setNoDelay(true);s.setTimeout(5000);
        s.once('connect',()=>{if(socket!==s)return;connected=true;s.setTimeout(0);resolve({connected:true});});
        s.on('timeout',()=>s.destroy(Error('Connection timed out')));
        s.on('error',e=>{if(socket===s){enqueue({event:'bridgeError',text:e.message});disconnect();}reject(e);});
        s.on('close',()=>{if(socket===s){disconnect();enqueue({event:'disconnected'});}reject(Error('Connection closed'));});
        s.on('data',data=>{if(socket!==s)return;buffer+=data;if(buffer.length>2*1024*1024){disconnect();return;}let at;while((at=buffer.indexOf('\n'))>=0){const line=buffer.slice(0,at);buffer=buffer.slice(at+1);try{const msg=JSON.parse(line);const p=waiting.get(msg.id);if(p){clearTimeout(p.timer);waiting.delete(msg.id);p.resolve(msg);}else enqueue(msg);}catch{enqueue({event:'bridgeError',text:'Invalid debugger message'});}}});
    });}
    const ack=new Set(['hello','breakpoints','ready','stack','variables','evaluate','profile']);
    const allowed=new Set([...ack,'pause','continue','next','stepIn','stepOut']);
    function command(cmd,args={}){if(!connected||!socket)throw Error('Connect first');if(!allowed.has(cmd))throw Error('Unsupported debugger command');if(waiting.size>=32)throw Error('Too many pending requests');
        const id=++seq, msg={cmd,id};
        if(cmd==='breakpoints'){if(typeof args.file!=='string'||args.file.length>512||!Array.isArray(args.lines)||args.lines.length>2048||args.lines.some(n=>!Number.isSafeInteger(n)||n<1||n>100000))throw Error('Invalid breakpoints');msg.file=args.file;msg.lines=args.lines;}
        if(cmd==='variables'){if(Number.isSafeInteger(args.ref)&&args.ref>0)msg.ref=args.ref;else{msg.frame=Number.isSafeInteger(args.frame)&&args.frame>=0?args.frame:0;msg.scope=args.scope==='globals'?'globals':'locals';}}
        if(cmd==='profile'){if(typeof args.on!=='boolean')throw Error('Choose whether to start or stop profiling');msg.on=args.on;}
        if(cmd==='evaluate'){if(typeof args.expr!=='string'||args.expr.length>512)throw Error('Invalid watch');msg.expr=args.expr;msg.frame=Number.isSafeInteger(args.frame)&&args.frame>=0?args.frame:0;}
        if(!ack.has(cmd)){socket.write(JSON.stringify(msg)+'\n');return Promise.resolve({sent:true});}
        return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{waiting.delete(id);reject(Error('Debugger request timed out'));},5000);waiting.set(id,{resolve,reject,timer});socket.write(JSON.stringify(msg)+'\n');});
    }
    async function validate(source,kind){if(!validator)throw Error('Start bridge with a native validator executable as the fourth argument');if(validating)throw Error('Native validation is already running');if(typeof source!=='string'||Buffer.byteLength(source)>2*1024*1024||!['Screen','Behaviour'].includes(kind))throw Error('Invalid native compile request');source=preview.validationSource(source);if(Buffer.byteLength(source)>2*1024*1024)throw Error('Native source and helper scripts exceed 2 MiB.');validating=true;try{return await new Promise((resolve,reject)=>{const p=spawn(validator,[kind],{stdio:['pipe','pipe','pipe'],windowsHide:true});let output='',timer=setTimeout(()=>p.kill(),15000);const append=b=>{if(output.length<128*1024)output+=String(b).slice(0,128*1024-output.length);};p.stdout.on('data',append);p.stderr.on('data',append);p.stdin.on('error',()=>{});p.on('error',e=>{clearTimeout(timer);reject(e);});p.on('close',(code,signal)=>{clearTimeout(timer);resolve({ok:code===0,output:output||('Native compiler exited '+(signal||code))});});p.stdin.end(source);});}finally{validating=false;}}
    function reply(res,status,value){res.writeHead(status,{'Content-Type':'application/json','Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});res.end(JSON.stringify(value));}
    const server=http.createServer(async(req,res)=>{
        const address=server.address(); const expected='127.0.0.1:'+address.port;
        if(req.headers.host!==expected){reply(res,403,{error:'Invalid host'});return;}
        if(req.method==='GET'&&req.url==='/'){res.writeHead(200,{'Content-Type':'text/html; charset=utf-8','Cache-Control':'no-store','X-Frame-Options':'DENY','Content-Security-Policy':"default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; media-src 'self' blob:; connect-src 'self'; object-src 'none'; frame-ancestors 'none'"});res.end(html);return;}
        if(req.method!=='POST'||!['/debug','/play','/cook'].includes(req.url)||req.headers.origin!=='http://'+expected||req.headers['x-nexgen-token']!==token){reply(res,403,{error:'Open Studio via the local bridge'});return;}
        try{lastContact=Date.now();if(req.url==='/play'||req.url==='/cook'){const chunks=[];let size=0;for await(const chunk of req){size+=chunk.length;if(size>100*1024*1024)throw Error('Project is too large.');chunks.push(chunk);}const body=JSON.parse(Buffer.concat(chunks).toString('utf8'));if(req.url==='/cook'){if(body.action==='status'){reply(res,200,{available:cooker.available});return;}if(body.action!=='pack')throw Error('Unsupported cook action');const r=await cooker.pack(body.project,body.platform);res.writeHead(200,{'Content-Type':'application/zip','Content-Length':r.bytes.length,'Cache-Control':'no-store','X-Content-Type-Options':'nosniff','X-Nexgen-Cooked':String(r.summary.cooked)});res.end(r.bytes);return;}let result;switch(body.action){case 'status':result=preview.status();break;case 'start':result=preview.start(body.project,body.screen);break;case 'reload':result=preview.reload(body.project);break;case 'stop':result=await preview.stop();break;case 'sceneStart':result=preview.startScene(body.project,body.screen,body.path,body.scene);break;case 'sceneRead':result=preview.readScene(body.ticket);break;case 'sceneStop':result=await preview.stopScene(body.ticket);break;default:throw Error('Unsupported play test action');}reply(res,200,result);return;}let data='';for await(const chunk of req){data+=chunk;if(data.length>3*1024*1024)throw Error('Request too large');}const body=JSON.parse(data);if(body.action!=='validate'&&data.length>65536)throw Error('Request too large');let result;
            switch(body.action){case 'validate':result=await validate(body.source,body.kind);break;case 'attach': events=[];result=await attach();break;case 'detach':disconnect();result={connected:false};break;case 'events':result={connected,events:events.splice(0)};break;case 'command':result=await command(body.cmd,body.args);break;default:throw Error('Unsupported action');}reply(res,200,result);
        }catch(e){reply(res,400,{error:e.message});}
    });
    // Explicit shutdown must release active HTTP connections, including incomplete
    // requests and keep-alive sockets. Waiting for server.close alone can hang.
    const httpSockets=new Set();
    server.on('connection',s=>{httpSockets.add(s);s.once('close',()=>httpSockets.delete(s));});
    let closing=null;
    function close(){
        if(closing)return closing;
        disconnect();
        clearInterval(idle);
        const closed=new Promise((resolve,reject)=>{
            server.close(e=>{if(e&&e.code!=='ERR_SERVER_NOT_RUNNING')reject(e);else resolve();});
            for(const s of httpSockets)s.destroy();
        });
        closing=Promise.all([closed,preview.stop(),cooker.stop()]);
        return closing;
    }
    const idle=setInterval(()=>{if(connected&&Date.now()-lastContact>90000)disconnect();},5000);idle.unref();server.on('close',()=>{clearInterval(idle);disconnect();preview.stop().catch(()=>{});cooker.stop().catch(()=>{});});
    return {server,token,start:()=>new Promise((resolve,reject)=>{server.once('error',reject);server.listen(webPort,'127.0.0.1',()=>resolve(server.address()));}),close};
}
if(require.main===module){const args=process.argv.slice(2);let cookExecutable=null;const ci=args.indexOf('--cook');if(ci>=0){cookExecutable=args[ci+1];args.splice(ci,2);if(!cookExecutable||cookExecutable.startsWith('--')){console.error('Pass the Nexgen cook executable after --cook.');process.exit(1);}}const i=args.indexOf('--preview-engine');let previewEngine=null;if(i>=0){previewEngine=args[i+1];args.splice(i,2);if(!previewEngine){console.error('Pass a desktop Nexgen executable after --preview-engine.');process.exit(1);}}const [host='127.0.0.1',port='4711',webPort='8765',validator]=args;if(args.length>4||!host||![port,webPort].every(x=>/^\d+$/.test(x)&&+x>0&&+x<=65535)){console.error('Usage: node debug_bridge.js [engine-host] [engine-port] [local-web-port] [native-validator-executable] [--preview-engine /path/to/test-copy/nexgen] [--cook /path/to/cook]');process.exit(1);}const b=createBridge({host,port:+port,webPort:+webPort,validator:validator?path.resolve(validator):null,previewEngine:previewEngine?path.resolve(previewEngine):null,cookExecutable:cookExecutable?path.resolve(cookExecutable):null});b.start().then(a=>console.log(`Blueprint Studio: http://127.0.0.1:${a.port}/ (target ${host}:${port}; trusted LAN only)`)).catch(e=>{console.error(e.message);process.exitCode=1;});let closing=false;const shutdown=async()=>{if(closing)return;closing=true;await b.close();process.exit(0);};process.on('SIGINT',shutdown);process.on('SIGTERM',shutdown);}
module.exports={createBridge};
