// Exercise the actual native debugger directly, independent of the legacy HTTP bridge.
const assert = require('node:assert/strict');
const net = require('node:net');
const {spawn} = require('node:child_process');
const B = require('./blueprint');
const fixture = process.argv[2];
const delay = ms => new Promise(r => setTimeout(r, ms));

async function port() {
    const s = net.createServer();
    await new Promise(r => s.listen(0, '127.0.0.1', r));
    const n = s.address().port;
    await new Promise(r => s.close(r));
    return n;
}
async function session(work) {
    const p = await port();
    const child = spawn(fixture, [String(p)], {stdio:['ignore','pipe','pipe']});
    let logs = '';
    child.stdout.on('data', b => logs += b);
    child.stderr.on('data', b => logs += b);
    let finished = false, exitCode = null;
    child.on('exit', code => {finished = true; exitCode = code;});
    let socket;
    try {
        for (let i=0; i<100 && !socket; ++i) {
            try {
                socket = await new Promise((resolve,reject) => {
                    const s=net.connect(p,'127.0.0.1');
                    s.once('error',reject);s.once('connect',()=>resolve(s));
                });
            } catch { await delay(30); }
        }
        assert.ok(socket, logs);
        let input='', messages=[], nextId=10;
        socket.on('data', b => {
            input += b.toString();
            let n;
            while ((n=input.indexOf('\n'))>=0) {
                const line=input.slice(0,n);input=input.slice(n+1);
                if(line)messages.push(JSON.parse(line));
            }
        });
        socket.on('error',()=>{});
        async function wait(predicate) {
            for(let i=0;i<250;++i) {
                const at=messages.findIndex(predicate);
                if(at>=0)return messages.splice(at,1)[0];
                assert.ok(!finished, 'Fixture exited before reply: '+logs);
                await delay(20);
            }
            throw Error('Timed out: '+logs);
        }
        function send(cmd,args={}){socket.write(JSON.stringify({cmd,...args})+'\n');}
        async function ask(cmd,args={}){const id=nextId++;send(cmd,{...args,id});return wait(x=>x.id===id);}
        async function exited() {
            for(let i=0;i<250&&!finished;++i)await delay(20);
            assert.equal(exitCode,0,logs);
            assert.ok(finished,logs);
        }
        await work({send,ask,wait,exited,close:()=>socket.destroy(),messages});
    } finally {
        if(socket)socket.destroy();
        if(!finished)child.kill();
    }
}
async function main() {
    const doc=require('./examples/gameplay.bp.json');
    const map=B.debugMap(B.traceCompile(doc).source);
    const after=map.find(x=>x.node==='unbind');
    assert.ok(after);
    await session(async ({ask,send,wait,exited})=>{
        const hello=await ask('hello');
        assert.equal(hello.sourceIdentity,true);
        assert.equal(hello.remoteTrace,true);
        assert.equal((await ask('trace',{on:true})).tracing,true);
        send('pause');await ask('ready');
        await wait(x=>x.event==='stopped');
        const first=(await ask('stack')).frames[0];
        assert.match(first.fingerprint,/^[0-9a-f]{16}$/);
        assert.equal(first.file,'showcase/gameplay/main.as');
        const initial=await ask('traceDrain');
        assert.ok(initial.trace.length>0&&initial.trace.length<=32);
        assert.equal(initial.trace[0].fingerprint,first.fingerprint);
        await ask('breakpoints',{file:first.file,lines:[after.line],fingerprint:first.fingerprint});
        send('continue');await wait(x=>x.event==='stopped');
        const current=(await ask('stack')).frames[0];
        assert.equal(current.line,after.line);
        assert.equal(current.fingerprint,first.fingerprint);
        const batch=await ask('traceDrain');
        assert.ok(batch.trace.length>0&&batch.trace.length<=32);
        const variables=await ask('variables',{frame:0,scope:'locals',limit:1});
        assert.ok(variables.variables.length<=1);
        await ask('clearBreakpoints');
        send('continue');await exited();
    });
    await session(async ({ask,send,exited,messages})=>{
        await ask('hello');
        await ask('breakpoints',{file:'showcase/gameplay/main.as',lines:[after.line],fingerprint:'0000000000000000'});
        await ask('ready');await exited();
        assert.ok(!messages.some(x=>x.event==='stopped'),'Wrong source identity paused the game');
    });
    await session(async ({ask,send,wait,close,exited})=>{
        await ask('hello');send('pause');await ask('ready');
        await wait(x=>x.event==='stopped');close();await exited();
    });
    console.log('PASS source-guarded breakpoint, bounded trace, locals limit, wrong identity, pause disconnect');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
