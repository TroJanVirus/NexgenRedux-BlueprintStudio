'use strict';
const assert=require('node:assert/strict'), S=require('./scene_editor'),P=require('./project'),B=require('./blueprint');
const scene={groups:['enemy'],entities:[{name:'Tree',place:[1,9,2],behaviour:'A',custom:{x:1}}]};
scene.entities[0]=S.move(scene.entities[0],'3D',4,5);
assert.deepEqual(scene.entities[0].place,[4,9,5]);assert.deepEqual(scene.entities[0].custom,{x:1});
assert.deepEqual(S.decode(S.encode(scene)),scene);assert.throws(()=>S.validate({entities:[null]}));
assert.equal(S.detectMode(scene),'3D');assert.equal(S.detectMode({entities:[{position:[1,2]}]}),'2D');assert.equal(S.uniqueName({entities:[{name:'Entity1'},{name:'Entity3'}]}),'Entity2');
assert.deepEqual(S.move(S.preset('2D','Box'),'2D',30,45).position,[30,45]);
assert.throws(()=>S.move({},'3D',Infinity,0));
for(const mode of ['2D','3D']){
 const d=JSON.parse(JSON.stringify(require('./examples/arcade.bp.json'))),g=d.graphs[0],n={id:'levelLoad',...S.loadNode(mode,'levels/forest.json')};
 assert.equal(B.describe(d,g,n).inputs[1].name,'name');assert.equal(n.defaults.name,'asset:levels/forest.json');
 const entry=g.nodes.find(x=>x.kind==='entry'); const existing=g.links.find(x=>x.from.node===entry.id&&x.from.pin==='then');g.links=g.links.filter(x=>x!==existing);g.nodes.push(n);g.links.push({from:{node:entry.id,pin:'then'},to:{node:n.id,pin:'in'}});if(existing)g.links.push({from:{node:n.id,pin:'then'},to:existing.to});
 const result=B.compile(d);assert.ok(result.ok,JSON.stringify(result.diagnostics));assert.ok(result.source.includes(mode==='3D'?'Space::load':'Scene::load'));assert.ok(result.source.includes('asset:levels/forest.json'));
 const packed=P.pack({version:1,kind:'BlueprintProject',name:'SceneTest',screens:[{id:'level',title:'Level',blueprint:d,assets:[{path:'levels/forest.json',data:S.encode(scene)}]}]});assert.ok(packed.entries.some(e=>e.path==='media/showcase/level/asset/levels/forest.json'));
}
console.log('PASS native scene model, round trip, load-node compilation and project packaging');
