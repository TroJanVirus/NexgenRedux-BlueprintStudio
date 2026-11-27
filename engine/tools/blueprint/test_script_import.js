'use strict';
const assert=require('node:assert/strict'),I=require('./script_import'),B=require('./blueprint');
const original='\uFEFF// Original comment\r\n'+`class Player : Screen {
 int health = 100;
 void onEnter() { health = 100; }
 void update(float delta) { if (Input::pressed("accept")) { TakeDamage(10); } }
 void TakeDamage(int damage) { this.health -= damage; if (health <= 0) { Die(); } }
 void Die() { health = 0; }
 int Score(int amount) { int next = amount + health; if (next > 100) { return 100; } return next; }
 void draw(Canvas@ c) { c.design(1280.0f,720.0f); }
}`.replace(/\n/g,'\r\n');
const result=I.read(original,'player.as');assert.equal(result.doc.importedScript.source,original);assert.equal(result.doc.importedScript.filename,'player.as');assert.equal(result.graphs,6);assert.equal(result.doc.variables[0].name,'health');assert.equal(result.doc.graphs.find(g=>g.name==='TakeDamage').nodes.some(n=>n.kind==='branch'),true);assert.equal(result.doc.graphs.find(g=>g.name==='Score').nodes.some(n=>n.kind==='sequence'),true);assert.equal(result.doc.graphs.find(g=>g.name==='update').nodes.some(n=>n.kind==='api:Input:Input::pressed:string'),true);assert.equal(B.compile(JSON.parse(JSON.stringify(result.doc))).source,result.source);assert.equal(result.doc.graphs.every(g=>g.nodes.every(n=>Number.isInteger(n.sourceLine))),true);
const behaviour=I.read('class Actor : Behaviour { int hits; void onHit(Entity@ target) { hits += 1; target.destroy(); } }');assert.equal(behaviour.doc.kind,'Behaviour');assert.match(behaviour.source,/bp_arg_other/);
const empty=I.read('class Blank : Screen { void onEnter() {} }');assert.equal(empty.nodes,1);
const choices=I.read('class Choice : Screen { int choose(bool yes) { if (yes) { return 1; } else { return 2; } } }');assert.equal(choices.doc.graphs[0].nodes.some(n=>n.kind==='sequence'),false);
const invalid=[
 '#include "global:scene.as"\nclass A : Screen { void onEnter() {} }',
 'class A : Screen { void onEnter() { while (true) {} } }',
 'class A : Screen { float x = -0.0f; void onEnter() {} }',
 'class A : Screen { float x = 1; void onEnter() {} }',
 'class A : Screen { int x; void onEnter() { x++; } }',
 'class A : Screen { int x; void onEnter() { x = Missing(); } }',
 'class A : Screen { void onEnter() {} void broken() { for (;;) {} } }',
 'class A : Screen { void onEnter() {} } class B : Screen {}',
 'class A : Screen { void onEnter() { return; } }',
 'class A : Screen { void onEnter() {} void again() { again(); } }',
 'class A : Screen { void onEnter() { }',
 'class A : Screen { void onEnter() { if (true) {} else {} }',
 'class A : Screen { float f = 0.0f; bool compare() { return f <= 1.0f; } }',
 'class A : Screen { int f(int &in x) { return x; } }',
 'class A : Screen { int f(int n) { n=0; return n; } }',
 'class A : Screen { int f() { if (true) { return 1; } } }',
 'class A : Screen { void onEnter() {} } /* unclosed',
 'class A : Screen { void onEnter() {} } garbage',
];for(const s of invalid)assert.throws(()=>I.read(s),/Line |Conversion refused|Integer|Graph|Invalid|Float/);
assert.throws(()=>I.read(' '.repeat(256*1024+1)),/256 KiB/);
assert.throws(()=>I.read('class A : Screen { void onEnter() { '+ '('.repeat(80)+'true'+')'.repeat(80)+'; } }'));
// Native validation is optional locally; the engine compiler checks both source forms.
if(process.env.NEXGEN_NATIVE_VALIDATE){const {spawnSync}=require('node:child_process');for(const [kind,script] of [['Screen',original],['Screen',result.source],['Behaviour',behaviour.doc.importedScript.source],['Behaviour',behaviour.source]]){const p=spawnSync(process.env.NEXGEN_NATIVE_VALIDATE,[kind],{input:script,encoding:'utf8'});assert.equal(p.status,0,p.stdout+p.stderr);}}
console.log('PASS source preservation, six editable graphs, native calls, locals, branches, Behaviour events and 20 refusal cases');
