'use strict';
const assert=require('node:assert/strict'),B=require('./blueprint'),P=require('./project'),S=require('./shared_behaviours');
const copy=x=>JSON.parse(JSON.stringify(x));
const screen=require('./examples/messages.bp.json'),b=screen.components[0],name=b.name;
let p={version:1,kind:'BlueprintProject',name:'Shared',screens:['one','two','local'].map(id=>({id,title:id,blueprint:copy(screen),assets:[]}))};
const baseline=B.compile(screen).source;
assert.equal(P.validateProject(p),p);
p=S.put(p,b,P.validateDocument);p.screens[0].blueprint=S.link(p,p.screens[0].blueprint,name);p.screens[1].blueprint=S.link(p,p.screens[1].blueprint,name);
assert.equal(S.uses(p,name).length,2);assert.equal(B.compile(S.resolve(p,p.screens[0].blueprint)).source,baseline);assert.ok(P.checkProject(p).every(r=>r.ok));
const changed=copy(b);changed.variables[0].default=17;
const before=copy(p);p=S.put(p,changed,P.validateDocument,true);assert.equal(before.screens[0].blueprint.components[0].variables[0].default,0);assert.equal(p.screens[0].blueprint.components[0].variables[0].default,17);assert.equal(p.screens[1].blueprint.components[0].variables[0].default,17);assert.equal(p.screens[2].blueprint.components[0].variables[0].default,0);
// A stale screen copy may never override the library, even from a file made by an older editor.
p.screens[0].blueprint.components[0]=copy(b);const compiled=P.checkProject(p);assert.ok(compiled.every(r=>r.ok));assert.equal(compiled[0].source,compiled[1].source);assert.notEqual(compiled[0].source,compiled[2].source);
const entries=P.pack(p).entries;const flattened=JSON.parse(entries.find(e=>e.path==='blueprints/one.bp.json').data);assert.equal(flattened.sharedComponents,undefined);assert.equal(flattened.components[0].variables[0].default,17);assert.equal(B.compile(flattened).source,compiled[0].source);
p.screens[1].blueprint=S.unlink(p,p.screens[1].blueprint,name);assert.equal(S.uses(p,name).length,1);changed.variables[0].default=29;p=S.put(p,changed,P.validateDocument,true);assert.equal(p.screens[1].blueprint.components[0].variables[0].default,17);assert.equal(p.screens[0].blueprint.components[0].variables[0].default,29);
assert.throws(()=>S.link(p,p.screens[2].blueprint,name),/different local/);assert.equal(S.link(p,p.screens[2].blueprint,name,true).components[0].variables[0].default,29);
assert.throws(()=>P.validateProject({...p,sharedBehaviours:[]}),/missing shared behaviour/);assert.throws(()=>P.validateProject({...p,sharedBehaviours:[changed,changed]}),/duplicate/);const bad=copy(p);bad.screens[0].blueprint.sharedComponents=null;assert.throws(()=>P.validateProject(bad),/Invalid shared behaviour links/);
const missing=copy(p);missing.screens[0].blueprint.components=missing.screens[0].blueprint.components.filter(c=>c.name!==name);assert.throws(()=>P.validateProject(missing),/component copy/);
const draft=copy(changed);draft.graphs[0].nodes.push({id:'unfinished',kind:'unknown'});p=S.put(p,draft,P.validateDocument,true);assert.equal(P.validateProject(p),p);assert.deepEqual(P.checkProject(p).map(r=>r.ok),[false,true,true]);assert.throws(()=>P.pack(p),/one:/);
// A linked behaviour uses the assets belonging to each screen, not another screen's files.
const media=copy(changed);media.graphs[0].nodes.push({id:'texture',kind:'api:Assets:Assets::texture:string',defaults:{name:'asset:textures/player.png'}});p=S.put(p,media,P.validateDocument,true);p.screens[0].assets=[{path:'textures/player.png',data:'YQ=='}];assert.equal(P.assetWarnings(p.screens[0]).length,0);p.screens[0].assets=[];assert.ok(P.checkProject(p)[0].diagnostics.some(d=>d.severity==='warning'&&d.message.includes('asset:textures/player.png')));
console.log('PASS shared behaviour linking, propagation, stale copies, local copies, standalone exports and unfinished drafts');
