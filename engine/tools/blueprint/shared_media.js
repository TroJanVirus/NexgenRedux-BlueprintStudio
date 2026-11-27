/* Shared files use Nexgen's existing global: paths. */
(function(root){'use strict';
const prefix='blueprint_';
const folder=m=>prefix+m.namespace;
const uri=(m,path)=>'global:'+folder(m)+'/'+path;
const library=p=>p.sharedMedia;
function validate(P,p){
 const m=library(p);if(m===undefined)return;
 if(!m||!P.isId(m.namespace)||!Array.isArray(m.assets))throw Error('Invalid shared media library');
 for(const a of m.assets){if(!a||!P.safeAsset(a.path)||new TextEncoder().encode('global/'+folder(m)+'/'+a.path).length>=192)throw Error('Unsafe or too long shared file path: '+a?.path);}
}
function stats(P,p){validate(P,p);return P.validateAssets([...p.screens,...(p.sharedMedia?[p.sharedMedia]:[])]);}
function find(p,name){const m=library(p);if(!m||typeof name!=='string'||!name.startsWith('global:'+folder(m)+'/'))return undefined;return m.assets.find(a=>uri(m,a.path)===name);}
function managed(p,name){const m=library(p);return !!m&&typeof name==='string'&&name.startsWith('global:'+folder(m)+'/');}
function choices(p,s){return [...(s.assets||[]).map(a=>({...a,uri:'asset:'+a.path})),...(library(p)?.assets||[]).map(a=>({...a,uri:uri(library(p),a.path)}))];}
function put(P,p,namespace,files,replace=false){
 if(!P.isId(namespace)||p.sharedMedia&&p.sharedMedia.namespace!==namespace)throw Error('The shared folder name cannot change once created.');
 const m=p.sharedMedia||{namespace,assets:[]};const keys=new Set();for(const a of files){if(!a||!P.safeAsset(a.path)||keys.has(a.path.toLowerCase()))throw Error('Unsafe or duplicate shared file: '+a?.path);keys.add(a.path.toLowerCase());}
 if(!replace&&m.assets.some(a=>keys.has(a.path.toLowerCase())))throw Error('A shared file already exists at that path.');
 // Preserve exact case on replacement so existing paths keep working on case-sensitive systems.
 const old=new Map(m.assets.map(a=>[a.path.toLowerCase(),a.path]));
 const next={...p,sharedMedia:{...m,assets:[...m.assets.filter(a=>!keys.has(a.path.toLowerCase())),...files.map(a=>({path:old.get(a.path.toLowerCase())||a.path,data:a.data}))]}};stats(P,next);if(P.assetStats)P.assetStats(next);return next;
}
function remove(P,p,path){if(!p.sharedMedia?.assets.some(a=>a.path===path))throw Error('Shared file not found.');const next={...p,sharedMedia:{...p.sharedMedia,assets:p.sharedMedia.assets.filter(a=>a.path!==path)}};stats(P,next);if(P.assetStats)P.assetStats(next);return next;}
// Only report references we can identify without evaluating script code.
function references(p,name){const hits=new Set();function contains(v){if(v===name)return true;if(Array.isArray(v))return v.some(contains);return !!v&&typeof v==='object'&&Object.values(v).some(contains);}
 for(const s of p.screens){if(contains(s.blueprint))hits.add(s.title);for(const a of s.assets||[]){if(!a.path.endsWith('.json')||a.data.length>2*1024*1024)continue;try{const bytes=typeof Buffer!=='undefined'?Buffer.from(a.data,'base64'):Uint8Array.from(atob(a.data),c=>c.charCodeAt(0));if(contains(JSON.parse(new TextDecoder().decode(bytes))))hits.add(s.title+' / '+a.path);}catch{}}}for(const b of p.sharedBehaviours||[])if(contains(b))hits.add(b.name);return [...hits];}
const api={folder,uri,library,validate,stats,find,managed,choices,put,remove,references};if(typeof module!=='undefined')module.exports=api;else root.NexgenSharedMedia=api;
})(globalThis);
