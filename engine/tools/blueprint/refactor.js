/* Move a self-contained chain into its own function without changing its order. */
(function(root){'use strict';
function extract(B,source,graphName,ids,name){
 const doc=JSON.parse(JSON.stringify(source)),g=doc.graphs.find(x=>x.name===graphName),set=new Set(ids);
 if(!g||g.kind==='macro'||g.pure)throw Error('Choose nodes in an event or ordinary function.');
 if(!B.ident(name)||B.members(doc,'graphs').some(x=>x.name===name)||B.members(doc,'variables').some(x=>x.name===name)||Object.hasOwn(B.events[doc.kind],name)||name===doc.name)throw Error('Choose a new function name.');
 if(!B.compile(doc).ok)throw Error('Compile and fix the graph before moving nodes.');
 const ns=g.nodes.filter(n=>set.has(n.id));if(ns.length<2||ns.length!==set.size)throw Error('Select at least two nodes.');
 const allowed=new Set(['get','set','constant','reroute','binary','unary','select','clamp','lerp','print','call','comment','convert','arrayLength','arrayGet','arraySet','arrayAdd','arrayRemove','arrayNew','construct','self','vec2','vec3','color']);
 const specs=new Map(ns.map(n=>[n.id,B.describe(doc,g,n)]));
 for(const n of ns){const s=specs.get(n.id);if(!s||!(allowed.has(n.kind)||n.kind.startsWith('api:'))||s.outputs.filter(p=>p.type==='exec').some(p=>p.name!=='then'))throw Error('Keep entry, return, loops, delays, stateful flow and local variables outside the selection.');}
 const linkType=l=>B.describe(doc,g,g.nodes.find(n=>n.id===l.from.node))?.outputs.find(p=>p.name===l.from.pin)?.type;
 const incoming=g.links.filter(l=>!set.has(l.from.node)&&set.has(l.to.node)),outgoing=g.links.filter(l=>set.has(l.from.node)&&!set.has(l.to.node));
 if(incoming.some(l=>linkType(l)!=='exec')||outgoing.some(l=>linkType(l)!=='exec')||incoming.length!==1||outgoing.length>1)throw Error('Select one complete action chain. Include its value nodes; leave just one incoming execution wire and at most one outgoing execution wire.');
 const chain=new Set();let id=incoming[0].to.node;while(id){if(chain.has(id))throw Error('Selection contains a loop.');chain.add(id);const next=g.links.filter(l=>l.from.node===id&&linkType(l)==='exec');if(next.length>1)throw Error('Choose a single action chain without branches.');id=next.length&&set.has(next[0].to.node)?next[0].to.node:null;}
 if(ns.some(n=>specs.get(n.id).inputs.some(p=>p.type==='exec')&&!chain.has(n.id)))throw Error('All selected actions must be on the same execution chain.');
 const used=new Set(g.nodes.map(n=>n.id));let count=0;const fresh=()=>{let id;do{id='tidy_'+(++count);}while(used.has(id));used.add(id);return id;};
 const minX=Math.min(...ns.map(n=>n.x||0)),minY=Math.min(...ns.map(n=>n.y||0));const entry={id:fresh(),kind:'entry',x:20,y:40},call={id:fresh(),kind:'call',ref:name,x:minX,y:minY};
 const f={kind:'function',name,params:[],returns:'void',folder:g.folder||'',nodes:[entry,...ns.map(n=>({...n,x:(n.x||0)-minX+300,y:(n.y||0)-minY+40}))],links:[{from:{node:entry.id,pin:'then'},to:incoming[0].to},...g.links.filter(l=>set.has(l.from.node)&&set.has(l.to.node))]};
 const complete=(g.frames||[]).filter(f=>f.nodes.every(id=>set.has(id)));if(complete.length)f.frames=complete;
 if(g.frames)g.frames=g.frames.map(f=>({...f,nodes:f.nodes.filter(id=>!set.has(id))})).filter(f=>f.nodes.length);
 g.nodes=g.nodes.filter(n=>!set.has(n.id));g.nodes.push(call);g.links=g.links.filter(l=>!set.has(l.from.node)&&!set.has(l.to.node));g.links.push({from:incoming[0].from,to:{node:call.id,pin:'in'}});if(outgoing.length)g.links.push({from:{node:call.id,pin:'then'},to:outgoing[0].to});doc.graphs.push(f);
 const result=B.compile(doc);if(!result.ok)throw Error('Cannot move this selection: '+result.diagnostics[0].message);return {doc,call:call.id};
}
if(typeof module!=='undefined')module.exports={extract};else root.NexgenRefactor={extract};
})(globalThis);
