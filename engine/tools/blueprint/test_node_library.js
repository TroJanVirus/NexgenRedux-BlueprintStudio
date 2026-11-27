'use strict';
const assert=require('node:assert/strict'),B=require('./blueprint'),L=require('./node_library');
const doc=require('./examples/arcade.bp.json'),items=B.palette(doc,doc.graphs[0]);
assert.equal(L.words('Space::getPosition'),'space get position');
const camera=L.search(items,'camera space');assert.ok(camera.length);assert.equal(camera[0].d.category,'Space');assert.match(camera[0].d.title,/camera/i);
const spawn=L.search(items,'Scene::spawn');assert.ok(spawn.length);assert.equal(spawn[0].d.category,'Scene');assert.match(spawn[0].d.title,/spawn/i);
for(const dir of ['in','out']){const port={dir,type:'Entity@'};const result=L.search(items,'',port);assert.ok(result.length);assert.ok(result.every(x=>(dir==='out'?x.d.inputs:x.d.outputs).some(p=>p.type===port.type)));}
const f={kind:'function',name:'AddScore',pure:true};const allowed=L.available(items,f);assert.ok(allowed.length);assert.ok(allowed.every(x=>x.n.kind==='return'||!x.d.inputs.some(p=>p.type==='exec')));assert.ok(L.available(items,{name:'AddScore'}).every(x=>x.n.kind!=='call'||x.n.ref!=='AddScore'));
assert.equal(L.search(items,'zzzzzz-unfindable').length,0);assert.equal(L.search(items,'').length,items.length);
assert.deepEqual(L.search(items,'camera space'),camera);assert.equal(B.compile(doc).ok,true);
console.log('PASS native node search, word order, namespaced calls, pin filtering and stable results');

for (const category of ['Xbox','Xbox360','XboxOne','Windows','Mac','Linux']) {
    const native=B.catalog.nodes.filter(n=>n.category===category);
    assert.ok(native.length>0,category);
    assert.ok(native.some(n=>n.call===category+'::get_available'));
}
assert.ok(B.catalog.enums.Tray.includes('TrayClosed'));
assert.ok(B.catalog.enums.XboxText.includes('XboxKernel'));
assert.ok(L.search(items,'xbox cpu temperature').some(x=>x.d.category==='Xbox'));
console.log('PASS native platform categories, availability checks and console enum types');
