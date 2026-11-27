/* Move one action chain into a function without changing when values are read. */
(function(root){'use strict';
function extract(B,source,graphName,ids,name){
 const doc=JSON.parse(JSON.stringify(source)),g=doc.graphs.find(x=>x.name===graphName),set=new Set(ids);
 if(!g||g.kind==='macro'||g.pure)throw Error('Choose nodes in an event or ordinary function.');
 if(!B.ident(name)||B.members(doc,'graphs').some(x=>x.name===name)||B.members(doc,'variables').some(x=>x.name===name)||Object.hasOwn(B.events[doc.kind],name)||name===doc.name)throw Error('Choose a new function name.');
 if(!B.compile(doc).ok)throw Error('Compile and fix the graph before moving nodes.');
 const ns=g.nodes.filter(n=>set.has(n.id));if(ns.length<2||ns.length!==set.size)throw Error('Select at least two nodes.');
 const allowed=new Set(['get','set','constant','reroute','add','subtract','multiply','divide','less','greater','equal','and','or','not','valid','toString','vector','component','select','clamp','lerp','print','call','comment','convert','arrayLength','arrayGet','arraySet','arrayAdd','arrayRemove','arrayNew','construct','self']);
 const specs=new Map(ns.map(n=>[n.id,B.describe(doc,g,n)]));
 for(const n of ns){const s=specs.get(n.id);if(!s||!(allowed.has(n.kind)||n.kind.startsWith('api:'))||s.outputs.filter(p=>p.type==='exec').some(p=>p.name!=='then'))throw Error('Keep entry, return, loops, delays, stateful flow and local variables outside the selection.');}
 const linkType=l=>B.describe(doc,g,g.nodes.find(n=>n.id===l.from.node))?.outputs.find(p=>p.name===l.from.pin)?.type;
 const incoming=g.links.filter(l=>!set.has(l.from.node)&&set.has(l.to.node)),outgoing=g.links.filter(l=>set.has(l.from.node)&&!set.has(l.to.node));
 const inExec=incoming.filter(l=>linkType(l)==='exec'),outExec=outgoing.filter(l=>linkType(l)==='exec');
 const inData=incoming.filter(l=>linkType(l)!=='exec'),outData=outgoing.filter(l=>linkType(l)!=='exec');
 if(inExec.length!==1||outExec.length>1)throw Error('Select one complete action chain with one incoming execution wire and at most one outgoing wire.');
 const byId=new Map(g.nodes.map(n=>[n.id,n])),portKey=p=>p.node+'|'+p.pin;
 const captured=n=>B.describe(doc,g,n)?.inputs.some(p=>p.type==='exec');
 // Function arguments are evaluated once, before the first action. Live getters and pure calls may change between actions.
 for(const l of inData){const n=byId.get(l.from.node);if(n.kind!=='entry'&&!captured(n)&&!(n.kind==='constant'&&!g.links.some(w=>w.to.node===n.id)))throw Error('Include live value nodes in the selection so they are still read at the right time.');}
 const outputs=[...new Map(outData.map(l=>[portKey(l.from),l])).values()];
 if(outputs.length>1)throw Error('A function can return one value. Select a smaller chain or pass extra values through variables.');
 if(outputs.some(l=>!captured(byId.get(l.from.node))))throw Error('A live value cannot become a captured function return. Leave that value node outside the selection.');
 const chain=new Set();let id=inExec[0].to.node;while(id){if(chain.has(id))throw Error('Selection contains a loop.');chain.add(id);const next=g.links.filter(l=>l.from.node===id&&linkType(l)==='exec');if(next.length>1)throw Error('Choose a single action chain without branches.');id=next.length&&set.has(next[0].to.node)?next[0].to.node:null;}
 if(ns.some(n=>specs.get(n.id).inputs.some(p=>p.type==='exec')&&!chain.has(n.id)))throw Error('All selected actions must be on the same execution chain.');
 const used=new Set(g.nodes.map(n=>n.id));let count=0;const fresh=()=>{let id;do{id='tidy_'+(++count);}while(used.has(id));used.add(id);return id;};
 const minX=Math.min(...ns.map(n=>n.x||0)),minY=Math.min(...ns.map(n=>n.y||0));const entry={id:fresh(),kind:'entry',x:20,y:40},call={id:fresh(),kind:'call',ref:name,x:minX,y:minY};
 const inputs=new Map();for(const l of inData){const key=portKey(l.from);if(!inputs.has(key)){const type=linkType(l),label=l.to.pin.replace(/[^a-zA-Z0-9_]/g,'_');let param='input_'+label,k=2;while([...inputs.values()].some(p=>p.name===param))param='input_'+label+'_'+k++;inputs.set(key,{name:param,type,from:l.from});}}
 const ret=outputs[0],params=[...inputs.values()].map(({name,type})=>({name,type}));
 const f={kind:'function',name,params,returns:ret?linkType(ret):'void',folder:g.folder||'',nodes:[entry,...ns.map(n=>({...n,x:(n.x||0)-minX+300,y:(n.y||0)-minY+40}))],links:[{from:{node:entry.id,pin:'then'},to:inExec[0].to},...g.links.filter(l=>set.has(l.from.node)&&set.has(l.to.node))]};
 for(const l of inData)f.links.push({from:{node:entry.id,pin:inputs.get(portKey(l.from)).name},to:l.to});
 if(ret){const last=[...chain].at(-1),returnNode={id:fresh(),kind:'return',x:Math.max(...f.nodes.map(n=>n.x||0))+300,y:40};f.nodes.push(returnNode);f.links.push({from:{node:last,pin:'then'},to:{node:returnNode.id,pin:'in'}},{from:ret.from,to:{node:returnNode.id,pin:'value'}});}

 const complete=(g.frames||[]).filter(f=>f.nodes.every(id=>set.has(id)));if(complete.length)f.frames=complete;
 if(g.frames)g.frames=g.frames.map(f=>({...f,nodes:f.nodes.filter(id=>!set.has(id))})).filter(f=>f.nodes.length);
 g.nodes=g.nodes.filter(n=>!set.has(n.id));g.nodes.push(call);g.links=g.links.filter(l=>!set.has(l.from.node)&&!set.has(l.to.node));g.links.push({from:inExec[0].from,to:{node:call.id,pin:'in'}});if(outExec.length)g.links.push({from:{node:call.id,pin:'then'},to:outExec[0].to});for(const p of inputs.values())g.links.push({from:p.from,to:{node:call.id,pin:p.name}});for(const l of outData)g.links.push({from:{node:call.id,pin:'result'},to:l.to});doc.graphs.push(f);
 const result=B.compile(doc);if(!result.ok)throw Error('Cannot move this selection: '+result.diagnostics[0].message);return {doc,call:call.id};
}
if(typeof module!=='undefined')module.exports={extract};else root.NexgenRefactor={extract};
})(globalThis);
