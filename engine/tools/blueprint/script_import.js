/* Read a small, explicit subset of AngelScript into ordinary Blueprint nodes.
   Anything outside that subset is refused, never dropped or guessed. */
(function(root){'use strict';
const B=typeof module!=='undefined'?require('./blueprint'):root.NexgenBlueprint;
const W=typeof module!=='undefined'?require('./workspace'):root.NexgenWorkspace;
const scalar=new Set(['bool','int','uint','float','double','string']);
const numeric=new Set(['int','uint','float','double']);
const precedence={'||':1,'&&':2,'==':3,'!=':3,'<':4,'>':4,'<=':4,'>=':4,'+':5,'-':5,'*':6,'/':6};
const operations={'+':'add','-':'subtract','*':'multiply','/':'divide','<':'less','>':'greater','==':'equal','!=':'equal','<=':'greater','>=':'less','&&':'and','||':'or'};
function read(source,filename='main.as'){
 if(typeof source!=='string'||source.length>256*1024||new TextEncoder().encode(source).length>256*1024||source.includes('\0'))throw Error('Import a UTF-8 script of at most 256 KiB.');
 // Match from the current offset; an unknown token is an error, not a gap.
 const token=/\s+|\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:[^"\\\r\n]|\\["\\nrt])*"|(?:\d+\.\d+|\d+)(?:[eE][+-]?\d+)?[fFuU]?|[A-Za-z_]\w*|::|==|!=|<=|>=|&&|\|\||\+=|-=|\*=|\/=|[{}();:,.@=+*/!<>-]/y;
 const tokens=[];let offset=0,line=1;
 while(offset<source.length){token.lastIndex=offset;const m=token.exec(source);if(!m)throw Error('Line '+line+': unsupported token near '+JSON.stringify(source.slice(offset,offset+24)));const v=m[0];if(!/^\s|^\/\/|^\/\*/.test(v))tokens.push({v,line});line+=(v.match(/\n/g)||[]).length;offset=token.lastIndex;if(tokens.length>24000)throw Error('Script has too many tokens for this importer.');}
 tokens.push({v:'<end>',line});let i=0;
 const at=()=>tokens[Math.min(i,tokens.length-1)],peek=()=>at().v,take=()=>{const t=at();if(t.v==='<end>')fail('Unexpected end of script',t);i++;return t;}, eat=v=>peek()===v&&(i++,true);
 const fail=(message,t=at())=>{throw Error('Line '+t.line+': '+message);};
 const expect=v=>{if(!eat(v))fail('Expected '+v+', found '+peek());};
 function name(){const t=take();if(!B.ident(t.v))fail('Unsupported or reserved name '+t.v,t);return t.v;}
 function type(allowVoid=false){const t=take();let v=t.v;if(eat('@'))v+='@';if(!(allowVoid&&v==='void')&&!scalar.has(v)&&!(v.endsWith('@')&&B.types.includes(v)))fail('Unsupported type '+v,t);return v;}
 function literal(){const t=at();let sign=eat('-')?-1:1;const v=take().v;let result;
  if(v==='true'||v==='false'){if(sign<0)fail('Invalid literal',t);result={type:'bool',value:v==='true'};}
  else if(v[0]==='"'){if(sign<0||/[^\x20-\x7e]/.test(v))fail('Unsupported string literal',t);result={type:'string',value:JSON.parse(v)};}
  else if(/^\d/.test(v)){let ty=/f$/i.test(v)?'float':/u$/i.test(v)?'uint':/[.e]/i.test(v)?'double':'int';if(/^0\d/.test(v)||ty==='uint'&&(/[.e]/i.test(v)||sign<0))fail('Unsupported numeric literal',t);result={type:ty,value:sign*Number(v.replace(/[fu]$/i,''))};}
  else fail('Expected a supported literal',t);
  if(Object.is(result.value,-0))fail('Signed zero is not preserved by the graph compiler',t);B.literal(result.type,result.value);return {...result,kind:'literal',line:t.line};
 }
 expect('class');const className=name();expect(':');const kind=take().v;if(!Object.hasOwn(B.events,kind))fail('Only a single Screen or Behaviour class is supported');expect('{');
 const doc={version:1,name:className,kind,variables:[],graphs:[],importedScript:{version:1,filename:String(filename).split(/[\\/]/).pop().slice(0,128)||'main.as',source}};
 const methods=[];
 while(peek()!=='}'){
  if(peek()==='<end>')fail('Unclosed class');const t=type(true),n=name();
  if(eat('(')){
   const params=[];if(peek()!==')')do{const ty=type(),p=name();params.push({name:p,type:ty});}while(eat(','));expect(')');
   const event=Object.hasOwn(B.events[kind],n),required=B.events[kind][n];if(event&&(t!=='void'||params.length!==required.length||params.some((p,j)=>p.type!==required[j][1])))fail('Event signature does not match Nexgen: '+n);
   if(new Set(params.map(p=>p.name)).size!==params.length)fail('Duplicate parameter');
   const g={name:n,kind:event?'event':'function',params:event?[]:params,returns:t,nodes:[],links:[]};doc.graphs.push(g);
   expect('{');const start=i;let depth=1;while(depth&&peek()!=='<end>'){const v=take().v;if(v==='{')depth++;if(v==='}')depth--;if(depth>32)fail('Nested blocks exceed import limit');}if(depth)fail('Unclosed function');methods.push({g,params,start,end:i-1});
  }else{
   if(!scalar.has(t))fail('Only scalar fields are currently supported');let value=B.defaultValue(t);if(eat('=')){const l=literal();if(l.type!==t)fail('Field initializer needs an exact type: '+t);value=l.value;}expect(';');doc.variables.push({name:n,type:t,default:value});
  }
  if(doc.graphs.length>64||doc.variables.length>128)fail('Class exceeds Blueprint limits');
 }
 expect('}');if(peek()!=='<end>')fail('Additional declarations, includes or classes are not supported');if(!methods.length)fail('No functions found');
 const fields=new Map(doc.variables.map(v=>[v.name,v.type])),functions=new Map(methods.filter(m=>m.g.kind==='function').map(m=>[m.g.name,m.g]));
 if(fields.size!==doc.variables.length||new Set(doc.graphs.map(g=>g.name)).size!==doc.graphs.length)fail('Duplicate or overloaded member');
 for(const m of methods){
  const {g,params,start,end}=m;i=start;const argumentsByName=new Map(params.map((p,j)=>[p.name,{type:p.type,pin:g.kind==='event'?B.events[kind][g.name][j][0]:p.name}]));const locals=new Map();let serial=0;
  const node=(kind,extra={},line=at().line)=>{if(g.nodes.length>=512)fail('Graph exceeds 512 nodes');const n={id:'n'+(++serial),kind,x:40,y:serial*80,sourceLine:line,...extra};g.nodes.push(n);if(!B.describe(doc,g,n))fail('No matching Blueprint node for '+kind);return n;};
  const entry=node('entry',{},tokens[start-1].line);
  const link=(a,b,p)=>g.links.push({from:{node:a.node,pin:a.pin},to:{node:b.id,pin:p}});
  const out=(n,p,type)=>({node:n.id,pin:p,type});
  const data=(n,p,a)=>{const spec=B.describe(doc,g,n).inputs.find(x=>x.name===p);if(!spec||spec.type!==a.type)fail('Expected '+(spec?.type||p)+', got '+a.type);if(a.literal)(n.defaults??={})[p]=a.value;else link(a,n,p);};
  const readRef=(n,explicit=false)=>{if(!explicit&&locals.has(n))return out(node('getLocal',{ref:n}),'value',locals.get(n));if(!explicit&&argumentsByName.has(n)){const p=argumentsByName.get(n);return out(entry,p.pin,p.type);}if(fields.has(n))return out(node('get',{ref:n}),'value',fields.get(n));if(!explicit&&kind==='Behaviour'&&n==='entity')return out(node('self'),'value','Entity@');fail('Unknown value '+n);};
  function math(op,a,b,line){if(a.type!==b.type)fail('Mixed types require an explicit conversion: '+a.type+' and '+b.type,{line});if(['<=','>='].includes(op)&&!['int','uint'].includes(a.type))fail('Non-integer '+op+' is not imported (NaN semantics)',{line});const n=node(operations[op],{type:a.type},line);data(n,'a',a);data(n,'b',b);let r=out(n,'result',B.describe(doc,g,n).outputs[0].type);if(['!=','<=','>='].includes(op)){const neg=node('not',{},line);data(neg,'value',r);r=out(neg,'result','bool');}return r;}
  // Calls may be expressions only if represented by a value node. Do not reorder side effects.
  function call(n,target,args,asAction,line){let candidates=[];if(!target&&!n.includes('::')&&functions.has(n)){const f=functions.get(n);if(asAction&&f.returns!=='void'&&!scalar.has(f.returns))fail('Discarded object results are not imported',{line});if(!asAction)fail('Calls to user functions are imported as actions, not nested expressions',{line});if(f.params.length!==args.length||f.params.some((p,j)=>p.type!==args[j].type))fail('Function argument types do not match '+n,{line});const nd=node('call',{ref:n},line);f.params.forEach((p,j)=>data(nd,p.name,args[j]));return {n:nd,type:f.returns};}
   const values=target?[target,...args]:args;candidates=B.catalog.nodes.filter(a=>a.call===n&&a.member===!!target&&!a.property&&!a.outputs.length&&a.inputs.length===values.length&&a.inputs.every((p,j)=>p.type===values[j].type));
   if(candidates.length!==1)fail('No unique native overload with exact argument types for '+n,{line});const a=candidates[0];if(asAction&&a.result!=='void'&&!scalar.has(a.result))fail('Discarded object results are not imported',{line});if(asAction?a.pure:!a.pure||a.result==='void')fail('Unsupported call position for '+n,{line});const nd=node(a.id,{},line);a.inputs.forEach((p,j)=>data(nd,p.name,values[j]));return asAction?{n:nd,type:a.result}:out(nd,'result',a.result);
  }
  function args(depth){expect('(');const a=[];if(peek()!==')')do{a.push(expr(1,depth+1));}while(eat(','));expect(')');return a;}
  function head(){const t=at();let n=name(),target=null;if(eat('::'))n+='::'+name();else if(eat('.')){target=readRef(n);n=name();}return {n,target,line:t.line};}
  function primary(depth){if(depth>48)fail('Expression is too deeply nested');const t=at();if(eat('(')){const r=expr(1,depth+1);expect(')');return r;}if(eat('!')){const r=primary(depth+1),n=node('not',{},t.line);data(n,'value',r);return out(n,'result','bool');}
   if(/^\d|^"|^(true|false|-)$/.test(peek())){const a=literal();return {literal:true,type:a.type,value:a.value};}
   if(numeric.has(peek())&&tokens[i+1]?.v==='('){const to=take().v;const a=args(depth);if(a.length!==1||!numeric.has(a[0].type))fail('Unsupported conversion',t);const n=node('convert',{type:a[0].type,to},t.line);data(n,'value',a[0]);return out(n,'result',to);}
   if(eat('this')){expect('.');return readRef(name(),true);}const h=head();if(peek()==='(')return call(h.n,h.target,args(depth),false,h.line);if(h.target||h.n.includes('::'))fail('Unsupported property read',t);return readRef(h.n);
  }
  function expr(min=1,depth=0){if(depth>48)fail('Expression is too deeply nested');let a=primary(depth);while(Object.hasOwn(precedence,peek())&&precedence[peek()]>=min){const t=take(),b=expr(precedence[t.v]+1,depth+1);a=math(t.v,a,b,t.line);}return a;}
  function assign(ref,local,a,line){const ty=local?locals.get(ref):fields.get(ref);if(!ty||ty!==a.type)fail('Assignment needs exact type '+(ty||'(unknown variable)'),{line});const n=node(local?'setLocal':'set',{ref},line);data(n,'value',a);return n;}
  function statements(stop,from,depth=0){if(depth>32)fail('Too many nested branches');let current=from,terminated=false;
   while(i<end&&peek()!==stop){if(terminated)fail('Unreachable statements are not imported');const t=at();
    if(eat('if')){expect('(');const condition=expr();expect(')');const branch=node('branch',{},t.line);data(branch,'condition',condition);expect('{');const yes=statements('}',out(branch,'true','exec'),depth+1);expect('}');let no=false;if(eat('else')){expect('{');no=statements('}',out(branch,'false','exec'),depth+1);expect('}');}terminated=yes&&no;if(i<end&&peek()!==stop){if(terminated)fail('Unreachable statements are not imported');const seq=node('sequence',{},t.line);link(current,seq,'in');link(out(seq,'first','exec'),branch,'in');current=out(seq,'second','exec');}else link(current,branch,'in');continue;}
    let nd;
    if(eat('return')){if(g.kind!=='function')fail('Explicit event returns are not imported',t);nd=node('return',{},t.line);if(g.returns!=='void')data(nd,'value',expr());expect(';');terminated=true;}
    else if(scalar.has(peek())){if(g.kind!=='function'||depth!==0)fail('Local declarations are supported only at function top level',t);const ty=type(),n=name();if(locals.has(n)||fields.has(n)||argumentsByName.has(n))fail('Shadowed local name '+n,t);expect('=');const a=expr();locals.set(n,ty);(g.locals??=[]).push({name:n,type:ty,default:B.defaultValue(ty)});nd=assign(n,true,a,t.line);expect(';');}
    else{let explicit=eat('this');if(explicit)expect('.');const h=head();if(explicit&&(h.target||h.n.includes('::')))fail('Unsupported this member access',t);const op=peek();if(['=','+=','-=','*=','/='].includes(op)){take();if(h.target||h.n.includes('::')||!explicit&&argumentsByName.has(h.n))fail('Unsupported assignment target',t);const local=!explicit&&locals.has(h.n);let a;if(op==='=')a=expr();else{const old=readRef(h.n,explicit);a=math(op[0],old,expr(),t.line);}nd=assign(h.n,local,a,t.line);expect(';');}
     else if(peek()==='('){const r=call(h.n,h.target,args(0),true,h.line);nd=r.n;expect(';');}else fail('Unsupported statement: '+peek(),t);}
    link(current,nd,'in');current=out(nd,'then','exec');
   }return terminated;
  }
  statements('}',out(entry,'then','exec'));if(i!==end)fail('Could not read the whole function');
  const sizes=Object.fromEntries(g.nodes.map(n=>[n.id,{w:230,h:190}]));const positions=W.arrange(g,sizes,70);for(const n of g.nodes)Object.assign(n,positions[n.id]);
 }
 const compiled=B.compile(doc);if(!compiled.ok||compiled.diagnostics.some(d=>d.severity==='warning'))throw Error('Conversion refused: '+compiled.diagnostics.map(d=>[d.graph,d.message].filter(Boolean).join(': ')).join('; '));
 return {doc,source:compiled.source,graphs:doc.graphs.length,nodes:doc.graphs.reduce((n,g)=>n+g.nodes.length,0)};
}
const api={read};if(typeof module!=='undefined')module.exports=api;else root.NexgenScriptImport=api;
})(globalThis);
