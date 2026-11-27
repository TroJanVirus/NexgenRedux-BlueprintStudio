/* A project owns its shared behaviours. Screen copies are for standalone export. */
(function(root){'use strict';
const copy=x=>JSON.parse(JSON.stringify(x));
const names=d=>d.sharedComponents===undefined?[]:d.sharedComponents;
function get(p,name){return (p.sharedBehaviours||[]).find(b=>b.name===name);}
function uses(p,name){return p.screens.filter(s=>names(s.blueprint).includes(name));}
function validate(p,validateDocument){
 const library=p.sharedBehaviours===undefined?[]:p.sharedBehaviours;
 if(!Array.isArray(library)||library.length>128)throw Error('Invalid shared behaviour library');
 const seen=new Set();for(const b of library){if(!b||b.kind!=='Behaviour'||seen.has(b.name)||b.sharedComponents!==undefined)throw Error('Invalid or duplicate shared behaviour');validateDocument(b);seen.add(b.name);}
 for(const s of p.screens){const links=names(s.blueprint);if(!Array.isArray(links)||new Set(links).size!==links.length||links.length>24)throw Error('Invalid shared behaviour links in '+s.id);for(const name of links){if(typeof name!=='string'||!seen.has(name))throw Error(s.id+': missing shared behaviour '+name);if((s.blueprint.components||[]).filter(b=>b.name===name).length!==1)throw Error(s.id+': missing or duplicate component copy '+name);}}
}
function resolve(p,d){if(!names(d).length)return d;const next={...d,components:(d.components||[]).map(b=>{if(!names(d).includes(b.name))return b;const c=get(p,b.name);if(!c)throw Error('Missing shared behaviour '+b.name);return copy(c);})};return next;}
function standalone(d){const next={...d};delete next.sharedComponents;return next;}
function link(p,d,name,replace=false){const b=get(p,name);if(!b)throw Error('Shared behaviour not found: '+name);if(names(d).includes(name))return resolve(p,d);const local=(d.components||[]).find(c=>c.name===name);if(local&&!replace&&JSON.stringify(local)!==JSON.stringify(b))throw Error('A different local behaviour has that name. Choose whether to replace it.');if(!local&&(d.components||[]).length>=24)throw Error('A screen can contain up to 24 behaviours.');const next=copy(d);next.components=local?next.components.map(c=>c.name===name?copy(b):c):[...(next.components||[]),copy(b)];next.sharedComponents=[...names(d),name];return next;}
function unlink(p,d,name){const next=copy(resolve(p,d));next.sharedComponents=names(d).filter(n=>n!==name);if(!next.sharedComponents.length)delete next.sharedComponents;return next;}
function put(p,b,validateDocument,replace=false){if(!b||b.kind!=='Behaviour'||b.sharedComponents!==undefined)throw Error('Choose a Behaviour Blueprint');validateDocument(b);const old=get(p,b.name);if(old&&!replace)throw Error('A shared behaviour already has that name');if(!old&&(p.sharedBehaviours||[]).length>=128)throw Error('Shared behaviour library is full');const sharedBehaviours=old?p.sharedBehaviours.map(x=>x.name===b.name?copy(b):x):[...(p.sharedBehaviours||[]),copy(b)];return refresh({...p,sharedBehaviours});}
function refresh(p){return {...p,screens:p.screens.map(s=>({...s,blueprint:resolve(p,s.blueprint)}))};}
const api={get,uses,validate,resolve,standalone,link,unlink,put,refresh,names};if(typeof module!=='undefined')module.exports=api;else root.NexgenSharedBehaviours=api;
})(globalThis);
