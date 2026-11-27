'use strict';
const assert=require('node:assert/strict'),B=require('./blueprint'),O=require('./organise'),original=require('./examples/arcade.bp.json');
const copy=x=>JSON.parse(JSON.stringify(x));
let d=copy(original),f=d.graphs.find(g=>g.name==='AddScore');
// The example has a wired argument and a call site.
const prior=f.params.map(p=>({...p}));console.log('Function inputs:',prior);
const p=prior[0];assert.ok(p);const changed=O.editSignature(B,d,f.name,[{old:p.name,name:'pointsToAdd',type:p.type},{old:null,name:'unused',type:'bool'},...prior.slice(1).map(p=>({...p,old:p.name}))],f.returns);
assert.ok(B.compile(changed).ok);assert.ok(changed.graphs.find(g=>g.name===f.name).links.some(l=>l.from.pin==='pointsToAdd'));assert.ok(changed.graphs.flatMap(g=>g.nodes).some(n=>n.kind==='call'&&n.ref===f.name));assert.equal(JSON.stringify(d),JSON.stringify(original));
assert.throws(()=>O.editSignature(B,d,f.name,[],f.returns),/Disconnect/);
assert.throws(()=>O.editSignature(B,d,f.name,[{old:p.name,name:p.name,type:'string'}],f.returns),/Disconnect/);
assert.throws(()=>O.editSignature(B,d,f.name,[{old:p.name,name:'then',type:p.type}],f.returns),/unique input/);
const g=d.graphs[0],base=B.compile(d).source;g.frames=[{title:'Inner',nodes:['spawn','name'],collapsed:true},{title:'Outer',nodes:['spawn','name','rect'],collapsed:true}];assert.equal(O.foldedGroups(g).length,1);assert.equal(O.foldedGroups(g)[0].frame.title,'Outer');assert.ok(O.boundary(g,new Set(g.frames[1].nodes)).inputs.length);assert.ok(O.boundary(g,new Set(g.frames[1].nodes)).outputs.length);assert.equal(B.compile(d).source,base);assert.ok(O.overlap(new Set(['a','b']),new Set(['b','c'])));assert.ok(!O.overlap(new Set(['a']),new Set(['a','b'])));
const svg=O.imageSvg({bounds:{x:0,y:0,right:400,bottom:200},nodes:[{x:0,y:0,w:218,h:80,headerHeight:24,header:'#123456',body:'#fff',border:'#aaa',text:'#000',font:12,title:'A < B & C',pins:[]}],groups:[],wires:[],background:'#fff',text:'#000',title:'Graph'});assert.ok(svg.includes('A &lt; B &amp; C'));assert.ok(svg.includes('viewBox="-40 -65 480 305"'));
console.log('PASS function inputs, safe rejection, nested groups and SVG export');
