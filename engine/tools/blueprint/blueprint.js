/* Nexgen Blueprint compiler. Shared by the offline editor and CLI. */
(function (root) {
    'use strict';
    const catalog = typeof module !== 'undefined' ? require('./catalog.js') : root.NexgenCatalog;
    const api = new Map(catalog.nodes.map(n => [n.id, n]));
    const types = [...new Set(['bool', 'int', 'uint', 'float', 'double', 'string', 'Vec2', 'Vec3', 'Color', ...['bool', 'int', 'uint', 'float', 'double', 'string', 'Vec2', 'Vec3', 'Color', 'Entity@'].map(t => 'array<' + t + '>@'), ...Object.keys(catalog.enums), ...catalog.nodes.flatMap(n => [n.result, ...n.inputs.map(p => p.type)])])].filter(t => t !== 'void');
    const events = { Screen: { onEnter: [], update: [['dt', 'float']], draw: [['canvas', 'Canvas@']], onSuspend: [], onResume: [], onExit: [] }, Behaviour: { start: [], update: [['dt', 'float']], onHit: [['other', 'Entity@']], onLeave: [['other', 'Entity@']], onGone: [] } };
    const pin = (name, type, def) => ({ name, type, ...(def === undefined ? {} : { default: def }) });
    const signature = (title, category, inputs = [], outputs = [], execIn = false, execOut = []) => ({ title, category, inputs: [...(execIn ? [pin('in', 'exec')] : []), ...inputs], outputs: [...execOut.map(x => pin(x, 'exec')), ...outputs] });
    // These methods belong to Nexgen's existing global:scene.as, not the native API catalogue.
    const levelNodes = new Map([
        ['levelLoad',['Load level','load','bool',[pin('name','string','asset:level.json')]]],
        ['levelUpdate',['Update level','update','string',[pin('dt','float'),pin('player','Entity@',null),pin('shot','bool',false),pin('shotAt','Vec3',[0,0,0])]]],
        ['levelHud',['Draw level HUD','drawHud','void',[pin('canvas','Canvas@')]]],
        ['levelFind',['Find level entity','find','Entity@',[pin('name','string')]]],
        ['levelIsOn',['Is level object on','isOn','bool',[pin('name','string')]]],
        ['levelNumber',['Set HUD number','set','void',[pin('name','string'),pin('value','float')]]],
        ['levelText',['Set HUD text','set','void',[pin('name','string'),pin('value','string')]]],
        ['levelShow',['Show level entity','show','void',[pin('name','string'),pin('seconds','float',.6),pin('how','string','')]]],
        ['levelHide',['Hide level entity','hide','void',[pin('name','string'),pin('seconds','float',.4)]]],
        ['levelClear',['Clear level','clear','void',[]]]
    ]);
    const maths = { add: '+', subtract: '-', multiply: '*', divide: '/', less: '<', greater: '>', equal: '==', and: '&&', or: '||' };
    function defaultValue(t) { if (t === 'bool')
        return false; if (t === 'string')
        return ''; if (t === 'Vec2')
        return [0, 0]; if (t === 'Vec3')
        return [0, 0, 0]; if (t === 'Color')
        return [1, 1, 1, 1]; if (t === 'Voice' || t === 'Tween')
        return null; if (t.endsWith('@'))
        return null; if (catalog.enums[t])
        return catalog.enums[t][0]; return 0; }
    const reserved = new Set('and or xor not if else for while do switch case break continue return class interface namespace enum typedef funcdef import from const void bool int int8 int16 int32 int64 uint uint8 uint16 uint32 uint64 float double true false null this super is cast auto private protected shared external final override abstract mixin get set property in out inout default try catch throw'.split(' '));
    function ident(s) { return typeof s === 'string' && /^[A-Za-z_][A-Za-z0-9_]{0,63}$/.test(s) && !reserved.has(s) && !s.startsWith('bp_') && !Object.hasOwn(Object.prototype, s); }
    function literal(t, v) { if (v === undefined)
        v = defaultValue(t); if (t.endsWith('@')) {
        if (v !== null)
            throw Error('Object defaults must be null');
        return 'null';
    } if (t === 'Voice' || t === 'Tween') {
        if (v !== null)
            throw Error('Use a constructor node');
        return t + '()';
    } if (t === 'bool') {
        if (typeof v !== 'boolean')
            throw Error('Expected Boolean');
        return String(v);
    } if (t === 'string') {
        if (typeof v !== 'string' || v.length > 8192 || /[\u0000-\u0008\u000b\u000c\u000e-\u001f]/.test(v))
            throw Error('Invalid string');
        return JSON.stringify(v).replace(/\u2028/g, '\\u2028').replace(/\u2029/g, '\\u2029');
    } if (catalog.enums[t]) {
        if (!catalog.enums[t].includes(v))
            throw Error('Invalid ' + t);
        return v;
    } if (t === 'Color' && ['White', 'Black', 'Red', 'Orange', 'Yellow', 'Green', 'Blue', 'Purple', 'Gray'].includes(v))
        return v; if (['Vec2', 'Vec3', 'Color'].includes(t)) {
        const n = t === 'Vec2' ? 2 : t === 'Vec3' ? 3 : 4;
        if (!Array.isArray(v) || v.length !== n)
            throw Error('Expected ' + n + ' components');
        return t + '(' + v.map(x => literal('float', x)).join(', ') + ')';
    } if (typeof v !== 'number' || !Number.isFinite(v))
        throw Error('Expected finite ' + t); if (t === 'int' || t === 'uint') {
        if (!Number.isInteger(v) || v < (t === 'int' ? -2147483648 : 0) || v > (t === 'int' ? 2147483647 : 4294967295))
            throw Error('Integer out of range');
        return t + '(' + v + ')';
    } if (t === 'float' || t === 'double') {
        if (t === 'float' && Math.abs(v) > 3.402823466e38)
            throw Error('Float out of range');
        let s = String(v);
        if (!/[.e]/i.test(s))
            s += '.0';
        return s + (t === 'float' ? 'f' : '');
    } throw Error('Unknown type ' + t); }
    function lineage(doc) { const chain=[], names=new Set(); let d=doc; while(d){if(chain.length>=8 || names.has(d.name)) throw Error('Invalid or cyclic Blueprint inheritance (maximum 8 classes)'); if(!ident(d.name)||d.kind!==doc.kind) throw Error('Parent class must have the same engine base'); names.add(d.name); chain.unshift(d); d=d.parent;} return chain; }
    function members(doc,key) { const map=new Map(); for(const d of lineage(doc)) for(const m of d[key]||[]) map.set(m.name,m); return [...map.values()]; }
    // Contracts are embedded snapshots, deduplicated by signature at module compile time.
    function hierarchyOrder(doc,h){
        if(!h||!ident(h.name)||!['2D','3D'].includes(h.mode)||!Array.isArray(h.entities)||h.entities.length<1||h.entities.length>32)throw Error('Invalid hierarchy definition');
        const byName=new Map(), three=h.mode==='3D', vector=three?'Vec3':'Vec2', components=new Set((doc.components||[]).map(c=>c.name));
        for(const e of h.entities){
            if(!e||!ident(e.name)||e.name.length>23||byName.has(e.name))throw Error('Invalid or duplicate hierarchy entity name (23 characters max)');
            byName.set(e.name,e);
            if(!Array.isArray(e.position)||!Array.isArray(e.scale)||e.rotation===undefined)throw Error('Hierarchy entity requires local transforms');
            literal(vector,e.position);literal(vector,e.scale);literal(three?'Vec3':'float',e.rotation);
            if(!Array.isArray(e.behaviours)||e.behaviours.length>8||new Set(e.behaviours).size!==e.behaviours.length||e.behaviours.some(c=>!components.has(c)))throw Error('Unknown or duplicate hierarchy Behaviour');
            const v=e.visual||{kind:'none'};
            if(![...(three?['shape','model']:['rect','disc','image']),'none'].includes(v.kind))throw Error('Invalid hierarchy visual');
            if(['shape','rect','disc'].includes(v.kind))literal('Color',v.color);
            if(v.kind==='shape'&&!catalog.enums.Solid.includes(v.shape))throw Error('Invalid hierarchy solid');
            if(v.kind==='rect'||v.kind==='image'){literal('Vec2',v.size);if(v.size.some(x=>x<0))throw Error('Invalid visual dimensions');}
            if(v.kind==='disc'){literal('float',v.radius);if(!Number.isFinite(v.radius)||v.radius<0)throw Error('Invalid visual radius');}
            if(['model','image'].includes(v.kind)){literal('string',v.asset);if(typeof v.asset!=='string'||!v.asset.trim()||v.asset.length>512)throw Error('Hierarchy visual needs an asset path');}
        }
        const roots=h.entities.filter(e=>e.parent===null);if(roots.length!==1)throw Error('Hierarchy needs exactly one root');
        const order=[], visiting=new Set(),visited=new Set();function visit(e){if(visiting.has(e.name))throw Error('Hierarchy parenting cycle');if(visited.has(e.name))return;visiting.add(e.name);if(e.parent!==null){const parent=byName.get(e.parent);if(!parent)throw Error('Unknown hierarchy parent');visit(parent);}visiting.delete(e.name);visited.add(e.name);order.push(e);}
        for(const e of h.entities)visit(e);return order;
    }
    function validateHierarchies(doc){if(doc.hierarchies===undefined)return;if(doc.kind!=='Screen'||!Array.isArray(doc.hierarchies)||doc.hierarchies.length>16)throw Error('Only Screen Blueprints support up to 16 entity hierarchies');const seen=new Set();for(const h of doc.hierarchies){if(seen.has(h.name))throw Error('Duplicate hierarchy name');seen.add(h.name);hierarchyOrder(doc,h);}}
    function interfaceRegistry(doc) {
        const result = new Map(), seen = new Set();
        function visit(d, depth) {
            if (!d || depth > 16 || seen.has(d)) throw Error('Invalid or cyclic interface document hierarchy');
            seen.add(d);
            if (seen.size > 128) throw Error('Too many embedded Blueprint classes');
            if (d.interfaces !== undefined && (!Array.isArray(d.interfaces) || d.interfaces.length > 32)) throw Error('Invalid interface list');
            const local = new Set();
            for (const i of d.interfaces || []) {
                if (!i || !ident(i.name) || local.has(i.name) || typeof i.implement !== 'boolean' || !Array.isArray(i.methods) || i.methods.length < 1 || i.methods.length > 32) throw Error('Invalid or duplicate interface');
                local.add(i.name);
                const methodNames = new Set();
                for (const m of i.methods) {
                    if (!m || !ident(m.name) || methodNames.has(m.name) || Object.hasOwn(events.Screen,m.name) || Object.hasOwn(events.Behaviour,m.name) || !['void',...types].includes(m.returns) || !Array.isArray(m.params) || m.params.length > 24) throw Error('Invalid interface method');
                    methodNames.add(m.name);
                    const names = new Set(['in','then','missing','target','result']);
                    for (const p of m.params) { if (!p || !ident(p.name) || names.has(p.name) || !types.includes(p.type)) throw Error('Invalid interface parameter'); names.add(p.name); }
                }
                const methods = i.methods.map(m=>({name:m.name,returns:m.returns,params:m.params.map(p=>({name:p.name,type:p.type}))})).sort((a,b)=>a.name.localeCompare(b.name));
                const previous = result.get(i.name);
                if (previous && JSON.stringify(previous.methods)!==JSON.stringify(methods)) throw Error('Conflicting interface definition '+i.name);
                result.set(i.name,{name:i.name,methods});
            }
            if(d.parent) visit(d.parent,depth+1);
            for(const c of d.components||[]) visit(c,depth+1);
        }
        visit(doc,0);
        if(result.size>64) throw Error('Too many interface definitions');
        return result;
    }
    function interfaceMethod(doc,n) { return interfaceRegistry(doc).get(n.ref)?.methods.find(m=>m.name===n.method); }
    function parentGraph(doc,g) {return doc.parent ? members(doc.parent,'graphs').find(x=>x.name===g.name&&x.kind===g.kind):null;}
    function compatibleHandler(f,d) { return f.kind === 'function' && f.returns === 'void' && f.params?.length === d.params.length && f.params.every((p,i) => p.type === d.params[i].type); }
    function describe(doc, g, n) {
        const k = n.kind;
        if (levelNodes.has(k)) {
            if(doc.kind!=='Screen')return null;
            const [title,method,ret,inputs]=levelNodes.get(k);
            return {...signature(title,'Native level',inputs,ret==='void'?[]:[pin('result',ret)],true,['then']),description:'Uses Nexgen’s original Level helper (global:scene.as).'};
        }
        if (api.has(k)) {
            const a = api.get(k);
            return { ...signature(a.title, a.category, a.inputs, [...(a.result === 'void' ? [] : [pin('result', a.result)]), ...(a.outputs || [])], !a.pure, a.pure ? [] : ['then']), description: a.description };
        }
        const v = members(doc,'variables').find(v => v.name === n.ref);
        const fn = members(doc,'graphs').find(x => x.kind === 'function' && x.name === n.ref);
        if (k==='parentCall' && g.kind!=='macro' && parentGraph(doc,g)) { const params=g.kind==='event'?events[doc.kind][g.name].map(([name,type])=>({name,type})):g.params; const ret=g.kind==='event'?'void':g.returns; return signature('Call Parent '+g.name,'Functions',params.map(p=>pin(p.name,p.type)),ret==='void'?[]:[pin('result',ret)],!g.pure,g.pure?[]:['then']); }
        const t = n.type || 'float';
        const local=(g.locals||[]).find(x=>x.name===n.ref);
        if(local&&k==='getLocal')return signature('Get Local '+local.name,'Local Variables',[],[pin('value',local.type)]);
        if(local&&k==='setLocal')return signature('Set Local '+local.name,'Local Variables',[pin('value',local.type)],[pin('value',local.type)],true,['then']);
        if(k==='forEach'&&t.startsWith('array<')&&types.includes(t))return signature('For Each '+t,'Flow',[pin('target',t)],[pin('element',t.slice(6,-2)),pin('index','uint')],true,['body','completed']);
        if(k==='while')return signature('While Loop (bounded)','Flow',[pin('condition','bool',false)],[],true,['body','completed']);
        if(k==='breakLoop'||k==='continueLoop')return signature(k==='breakLoop'?'Break Loop':'Continue Loop','Flow',[],[],true,[]);
        if(k==='switch'&&['int','uint','string'].includes(t)&&Array.isArray(n.cases)&&n.cases.length>=1&&n.cases.length<=16)return signature('Switch '+t,'Flow',[pin('value',t)],[],true,[...n.cases.map((_,i)=>'case'+i),'default']);
        const macro=members(doc,'graphs').find(x=>x.kind==='macro'&&x.name===n.ref);
        if(k==='macroCall'&&macro)return signature('Macro '+macro.name,'Macros',(macro.params||[]).map(p=>pin(p.name,p.type)),(macro.results||[]).map(p=>pin(p.name,p.type)),true,macro.exits||['then']);
        if(k==='macroExit'&&g.kind==='macro'&&(g.exits||[]).includes(n.ref))return signature('Exit '+n.ref,'Macros',(g.results||[]).map(p=>pin(p.name,p.type)),[],true,[]);
        if(k==='macroEnter')return signature('Macro entry','Macros',[],[],true,['then']);
        if(k==='comment')return signature(typeof n.text==='string'?n.text.slice(0,160):'Comment','Comment');
        const dispatcher = (doc.dispatchers || []).find(x => x.name === n.ref);
        if (dispatcher && k === 'broadcast')
            return signature('Broadcast ' + n.ref, 'Dispatchers', dispatcher.params.map(p => pin(p.name, p.type)), [], true, ['then']);
        if (dispatcher && ['bind','unbind'].includes(k) && (doc.graphs || []).some(f => compatibleHandler(f, dispatcher) && f.name === n.handler))
            return signature((k === 'bind' ? 'Bind ' : 'Unbind ') + n.ref + ' → ' + n.handler, 'Dispatchers', [], [], true, ['then']);
        if (dispatcher && k === 'unbindAll')
            return signature('Unbind All ' + n.ref, 'Dispatchers', [], [], true, ['then']);
        if(k==='hierarchySpawn'&&doc.kind==='Screen'){
            const h=(doc.hierarchies||[]).find(x=>x.name===n.ref);
            if(h)return signature('Spawn '+h.name,'Entity Hierarchies',[pin('origin',h.mode==='3D'?'Vec3':'Vec2')],[pin('root','Entity@'),...h.entities.map(e=>pin('entity_'+e.name,'Entity@'))],true,['then','failed']);
        }
        if(k==='stateMachine' && (g.kind==='macro'||g.kind==='event'&&g.name==='update')) {
            const mode=n.mode||'logic';
            if(!Array.isArray(n.states)||!Array.isArray(n.transitions))return null;
            return signature(mode==='sprite'?'Sprite Animation State Machine':mode==='model'?'Model Animation State Machine':'State Machine','State Machines',
                [...(mode==='logic'?[]:[pin('target','Entity@')]),pin('dt','float',0),pin('reset','bool',false),...n.transitions.map(t=>pin('when_'+t.name,'bool',false))],
                [pin('activeState','string'),pin('entered','bool'),pin('elapsed','float')],true,[...n.states.map(s=>'state_'+s.name),'then']);
        }
        if (k==='interfaceMessage') {
            const m=interfaceMethod(doc,n);
            if(m) return signature(n.ref+' → '+m.name,'Interfaces',[pin('target','Entity@'),...m.params.map(p=>pin(p.name,p.type))],m.returns==='void'?[]:[pin('result',m.returns)],true,['then','missing']);
        }
        if (k === 'timeline' && (g.kind === 'macro' || g.kind === 'event' && !['draw','onExit','onGone'].includes(g.name)))
            return signature('Timeline (Float)', 'Flow', [pin('duration','float',1),pin('from','float',0),pin('to','float',1),pin('smooth','bool',false)], [pin('value','float'),pin('alpha','float'),...(n.tracks||[]).map(t=>pin(t.name,t.type))], true, ['update','finished']);
        if (k === 'gate') return signature('Gate', 'Flow', [pin('open','bool',true)], [], true, ['then']);
        if (k === 'doN') return signature('Do N', 'Flow', [pin('count','int',1),pin('reset','bool',false)], [pin('counter','int')], true, ['then']);
        if (k === 'reroute' && types.includes(t)) return signature('Reroute ' + t, 'Values', [pin('value',t)], [pin('result',t)]);
        if (k === 'clamp' && ['float','int','uint','double'].includes(t)) return signature('Clamp ('+t+')','Math',[pin('value',t),pin('min',t),pin('max',t,1)],[pin('result',t)]);
        if (k === 'lerp') return signature('Lerp (float)','Math',[pin('a','float'),pin('b','float',1),pin('alpha','float')],[pin('result','float')]);
        if (k === 'entry') {
            let p = g.kind === 'event' ? events[doc.kind]?.[g.name] : g.params?.map(p => [p.name, p.type]);
            return signature(g.name, 'Events', [], (p || []).map(x => pin(...x)), false, ['then']);
        }
        if (k === 'get' && v)
            return signature('Get ' + v.name, 'Variables', [], [pin('value', v.type)]);
        if (k === 'set' && v)
            return signature('Set ' + v.name, 'Variables', [pin('value', v.type)], [pin('value', v.type)], true, ['then']);
        if (k === 'self' && doc.kind === 'Behaviour')
            return signature('Self Entity', 'Entity', [], [pin('value', 'Entity@')]);
        if (k === 'constant' && types.includes(t))
            return signature(t + ' Literal', 'Values', [pin('value', t)], [pin('value', t)]);
        if (k === 'branch')
            return signature('Branch', 'Flow', [pin('condition', 'bool')], [], true, ['true', 'false']);
        if (k === 'sequence')
            return signature('Sequence', 'Flow', [], [], true, ['first', 'second', 'third']);
        if (k === 'repeat')
            return signature('For Loop (count)', 'Flow', [pin('count', 'int', 1)], [pin('index', 'int')], true, ['body', 'completed']);
        if (k === 'delay' && (g.kind === 'macro' || g.kind === 'event' && !['draw', 'onExit', 'onGone'].includes(g.name)))
            return signature('Delay', 'Flow', [pin('seconds', 'float', 1)], [], true, ['then']);
        if (k === 'attach' && (doc.components || []).some(c => c.name === n.ref))
            return signature('Attach ' + n.ref, 'Components', [pin('target', 'Entity@')], [], true, ['then']);
        if (k === 'once')
            return signature('Do Once', 'Flow', [pin('reset', 'bool', false)], [], true, ['then']);
        if (k === 'flipflop')
            return signature('Flip Flop', 'Flow', [], [], true, ['a', 'b']);
        if (k === 'return' && g.kind === 'function')
            return signature('Return', 'Functions', g.returns === 'void' ? [] : [pin('value', g.returns)], [], true, []);
        if (k === 'call' && fn)
            return signature((fn.pure?'Pure ':'Call ') + fn.name, 'Functions', (fn.params || []).map(p => pin(p.name, p.type)), fn.returns === 'void' ? [] : [pin('result', fn.returns)], !fn.pure, fn.pure?[]:['then']);
        if (k === 'construct' && ['Json@', 'Look@', 'Path@', 'Mesh@', 'Message@', 'Room@', 'Connection@', 'Datagrams@', 'Voice', 'Tween'].includes(t))
            return signature('New ' + t.replace('@', ''), 'Objects', [], [pin('result', t)], true, ['then']);
        if (k === 'arrayNew' && t.startsWith('array<') && types.includes(t))
            return signature('Make ' + t, 'Arrays', [pin('length', 'uint', 0)], [pin('result', t)], true, ['then']);
        if (k === 'arrayLength' && t.startsWith('array<') && types.includes(t))
            return signature('Array Length', 'Arrays', [pin('target', t)], [pin('result', 'uint')]);
        if (k === 'arrayGet' && t.startsWith('array<') && types.includes(t))
            return signature('Array Get', 'Arrays', [pin('target', t), pin('index', 'uint')], [pin('result', t.slice(6, -2))]);
        if (k === 'arrayAdd' && t.startsWith('array<') && types.includes(t))
            return signature('Array Add', 'Arrays', [pin('target', t), pin('value', t.slice(6, -2))], [], true, ['then']);
        if (k === 'arraySet' && t.startsWith('array<') && types.includes(t))
            return signature('Array Set', 'Arrays', [pin('target', t), pin('index', 'uint'), pin('value', t.slice(6, -2))], [], true, ['then']);
        if (k === 'arrayRemove' && t.startsWith('array<') && types.includes(t))
            return signature('Array Remove', 'Arrays', [pin('target', t), pin('index', 'uint')], [], true, ['then']);
        if (k === 'print')
            return signature('Print String', 'Debug', [pin('text', 'string')], [], true, ['then']);
        if (k === 'valid' && t.endsWith('@'))
            return signature('Is Valid', 'Values', [pin('value', t)], [pin('result', 'bool')]);
        if (k === 'not')
            return signature('Not', 'Math', [pin('value', 'bool')], [pin('result', 'bool')]);
        if (k === 'toString' && ['int', 'uint', 'float', 'double', 'bool', 'string'].includes(t))
            return signature('To String (' + t + ')', 'Math', [pin('value', t)], [pin('result', 'string')]);
        if (k === 'convert' && ['int', 'uint', 'float', 'double'].includes(t) && ['int', 'uint', 'float', 'double'].includes(n.to))
            return signature(t + ' → ' + n.to, 'Math', [pin('value', t)], [pin('result', n.to)]);
        if (k === 'select' && types.includes(t))
            return signature('Select ' + t, 'Math', [pin('condition', 'bool'), pin('a', t), pin('b', t)], [pin('result', t)]);
        if (k === 'vector' && ['Vec2', 'Vec3', 'Color'].includes(t)) {
            const parts = t === 'Vec2' ? ['x', 'y'] : t === 'Vec3' ? ['x', 'y', 'z'] : ['r', 'g', 'b', 'a'];
            return signature('Make ' + t, 'Math', parts.map(x => pin(x, 'float', x === 'a' ? 1 : 0)), [pin('result', t)]);
        }
        if (k === 'component' && ['Vec2', 'Vec3', 'Color'].includes(t)) {
            const parts = t === 'Vec2' ? ['x', 'y'] : t === 'Vec3' ? ['x', 'y', 'z'] : ['r', 'g', 'b', 'a'];
            if (parts.includes(n.ref))
                return signature('Get ' + t + '.' + n.ref, 'Math', [pin('value', t)], [pin('result', 'float')]);
        }
        if (Object.hasOwn(maths, k) && (['int', 'uint', 'float', 'double'].includes(t) && !['and', 'or'].includes(k) || t === 'bool' && ['and', 'or', 'equal'].includes(k) || t === 'string' && ['add', 'equal'].includes(k))) {
            return signature(k + ' (' + t + ')', 'Math', [pin('a', t), pin('b', t, k === 'divide' ? 1 : undefined)], [pin('result', ['less', 'greater', 'equal', 'and', 'or'].includes(k) ? 'bool' : t)]);
        }
        return null;
    }
    function palette(doc, g) { if(g.handwritten)return []; let list = [{ kind: 'branch' }, { kind: 'sequence' }, { kind: 'repeat' }, { kind: 'once' }, { kind: 'flipflop' }, { kind: 'print' }, { kind: 'not' }, ...(doc.kind === 'Behaviour' ? [{ kind: 'self' }] : []), ...(g.kind === 'function' ? [{ kind: 'return' }] : [])]; if (g.kind === 'macro' || g.kind === 'event' && !['draw', 'onExit', 'onGone'].includes(g.name))
        list.push({ kind: 'delay' }, {kind:'timeline'}); list.push({kind:'gate'},{kind:'doN'},{kind:'lerp'},{kind:'comment'},{kind:'while'},{kind:'breakLoop'},{kind:'continueLoop'}); for(const t of ['int','uint','string'])list.push({kind:'switch',type:t,cases:t==='string'?['A','B']:[0,1]});for(const v of g.locals||[])list.push({kind:'getLocal',ref:v.name},{kind:'setLocal',ref:v.name});
        for(const d of doc.dispatchers || []) { list.push({kind:'broadcast',ref:d.name},{kind:'unbindAll',ref:d.name}); for(const f of doc.graphs.filter(f=>compatibleHandler(f,d))) list.push({kind:'bind',ref:d.name,handler:f.name},{kind:'unbind',ref:d.name,handler:f.name}); }
        if(doc.kind==='Screen')for(const kind of levelNodes.keys())list.push({kind});
        for(const h of doc.hierarchies||[])list.push({kind:'hierarchySpawn',ref:h.name});
        if(g.kind==='macro'||g.kind==='event'&&g.name==='update')for(const mode of ['logic','sprite','model'])list.push({kind:'stateMachine',mode,initial:'Idle',states:[{name:'Idle',clip:'idle',first:0,last:0,fps:8,loop:true,blend:0.25,speed:1},{name:'Move',clip:'walk',first:1,last:4,fps:8,loop:true,blend:0.25,speed:1}],transitions:[{name:'StartMove',from:'Idle',to:'Move'},{name:'StopMove',from:'Move',to:'Idle'}]});
        for(const i of interfaceRegistry(doc).values()) for(const m of i.methods) list.push({kind:'interfaceMessage',ref:i.name,method:m.name});
        for (const c of doc.components || [])
        list.push({ kind: 'attach', ref: c.name }); for (const v of members(doc,'variables'))
        list.push({ kind: 'get', ref: v.name }, { kind: 'set', ref: v.name }); if(parentGraph(doc,g)&&g.kind!=='macro') list.push({kind:'parentCall'}); for(const m of members(doc,'graphs').filter(x=>x.kind==='macro'&&x.name!==g.name))list.push({kind:'macroCall',ref:m.name}); for (const f of members(doc,'graphs'))
        if (f.kind === 'function')
            list.push({ kind: 'call', ref: f.name }); for (const t of types) {
        list.push({ kind: 'constant', type: t }, { kind: 'select', type: t },{kind:'reroute',type:t}); if (['int','uint','float','double'].includes(t)) list.push({kind:'clamp',type:t});
        if (t.endsWith('@'))
            list.push({ kind: 'valid', type: t });
        if (['Json@', 'Look@', 'Path@', 'Mesh@', 'Message@', 'Room@', 'Connection@', 'Datagrams@', 'Voice', 'Tween'].includes(t))
            list.push({ kind: 'construct', type: t });
        if (t.startsWith('array<'))
            for (const kind of ['arrayNew', 'arrayLength', 'arrayGet', 'arrayAdd', 'arraySet', 'arrayRemove','forEach'])
                list.push({ kind, type: t });
        if (['int', 'uint', 'float', 'double', 'bool', 'string'].includes(t)) {
            list.push({ kind: 'toString', type: t });
            for (const k of Object.keys(maths))
                list.push({ kind: k, type: t });
        }
        if (['int', 'uint', 'float', 'double'].includes(t))
            for (const to of ['int', 'uint', 'float', 'double'])
                if (t !== to)
                    list.push({ kind: 'convert', type: t, to });
        if (['Vec2', 'Vec3', 'Color'].includes(t)) {
            list.push({ kind: 'vector', type: t });
            for (const ref of ['x', 'y', 'z', 'r', 'g', 'b', 'a'])
                list.push({ kind: 'component', type: t, ref });
        }
    } return [...list, ...catalog.nodes.map(x => ({ kind: x.id }))].map(n => ({ n, d: describe(doc, g, n) })).filter(x => x.d); }
    // Expand reusable macros at compile time. Every call gets its own nodes/state;
    // the existing type/path/cycle verifier checks the resulting executable graph.
    function validateKeys(keys){if(!Array.isArray(keys)||keys.length<2||keys.length>32||keys[0].time!==0||keys.at(-1).time!==1)throw Error('Timeline curve needs 2–32 keys, starting at 0 and ending at 1');let prev=-1;for(const k of keys){literal('float',k.time);literal('float',k.value);if(k.time<=prev||k.time<0||k.time>1||!['linear','smooth','step'].includes(k.mode||'linear'))throw Error('Timeline key times must increase and use a valid interpolation');prev=k.time;}return true;}
    function validateMachine(n) {
        const mode=n.mode||'logic';
        if(!['logic','sprite','model'].includes(mode)||!Array.isArray(n.states)||n.states.length<1||n.states.length>16||!Array.isArray(n.transitions)||n.transitions.length>32)throw Error('Invalid state machine size or mode');
        const names=new Set();for(const state of n.states){
            if(!state||!ident(state.name)||names.has(state.name))throw Error('Invalid or duplicate state');names.add(state.name);
            if(mode!=='logic'&&typeof state.loop!=='boolean')throw Error('Invalid animation loop flag');
            if(mode==='sprite'){literal('uint',state.first);literal('uint',state.last);literal('float',state.fps);if(!Number.isInteger(state.first)||!Number.isInteger(state.last)||!Number.isFinite(state.fps)||state.last<state.first||state.last>65535||state.fps<=0||state.fps>240)throw Error('Invalid sprite animation range or FPS');}
            if(mode==='model'){literal('string',state.clip);literal('float',state.blend);literal('float',state.speed);if(!state.clip||state.clip.length>128||!Number.isFinite(state.blend)||!Number.isFinite(state.speed)||state.blend<0||state.blend>60||state.speed<0||state.speed>100)throw Error('Invalid model animation clip or playback');}
        }
        if(!names.has(n.initial))throw Error('Initial state must exist');
        const transitions=new Set();for(const t of n.transitions){if(!t||!ident(t.name)||transitions.has(t.name)||(!names.has(t.from)&&t.from!=='*')||!names.has(t.to)||t.from===t.to)throw Error('Invalid or duplicate state transition');transitions.add(t.name);}
    }
    function validateTimeline(n) {
        if(n.keys!==undefined) validateKeys(n.keys);
        if(n.playback!==undefined && !['forward','reverse','loop','pingpong'].includes(n.playback)) throw Error('Invalid timeline playback');
        if(n.tracks===undefined) return;
        if(!Array.isArray(n.tracks)||n.tracks.length>8) throw Error('Timeline supports up to 8 additional tracks');
        const names=new Set(['in','value','alpha','duration','from','to','smooth','update','finished']);
        for(const t of n.tracks){
            const count={float:1,Vec2:2,Vec3:3,Color:4}[t.type];
            if(!ident(t.name)||names.has(t.name)||!count||!Array.isArray(t.channels)||t.channels.length!==count) throw Error('Invalid or duplicate timeline track');
            names.add(t.name);for(const keys of t.channels)validateKeys(keys);
        }
    }
    function validateMacro(doc,g){
        if(!Array.isArray(g.params)||!Array.isArray(g.results)||!Array.isArray(g.exits)||g.params.length>24||g.results.length>24||!g.exits.length||g.exits.length>8||g.results.length&&g.exits.length!==1)throw Error('Macro requires 1–8 exits; typed results require a single exit: '+g.name);
        const params=new Set(['in','then']);for(const p of g.params){if(!ident(p.name)||params.has(p.name)||!types.includes(p.type))throw Error('Invalid macro parameter '+p.name);params.add(p.name);}
        const resultNames=new Set(g.exits);if(resultNames.size!==g.exits.length||g.exits.some(x=>!ident(x)))throw Error('Invalid macro exit name');
        for(const p of g.results){if(!ident(p.name)||resultNames.has(p.name)||p.name==='in'||!types.includes(p.type))throw Error('Invalid macro result '+p.name);resultNames.add(p.name);}
        if(!Array.isArray(g.nodes)||!Array.isArray(g.links)||g.nodes.length>512||g.links.length>2048)throw Error('Invalid macro graph '+g.name);
        const specs=new Map();for(const n of g.nodes){if(!/^[A-Za-z0-9_-]{1,64}$/.test(n.id)||specs.has(n.id)||n.kind==='parentCall'||n.kind==='return')throw Error('Invalid macro node '+n.id);const d=describe(doc,g,n);if(!d)throw Error('Unknown macro node '+n.kind);specs.set(n.id,d);for(const p of d.inputs)if(p.type!=='exec'&&Object.hasOwn(n.defaults||{},p.name))literal(p.type,n.defaults[p.name]);if(n.kind==='timeline')validateTimeline(n);if(n.kind==='stateMachine')validateMachine(n);if(n.kind==='switch'){const vals=n.cases.map(v=>literal(n.type,v));if(new Set(vals).size!==vals.length)throw Error('Duplicate switch case');}}
        if(g.nodes.filter(n=>n.kind==='entry').length!==1||g.exits.some(x=>g.nodes.filter(n=>n.kind==='macroExit'&&n.ref===x).length!==1))throw Error('Macro needs one entry and one node per exit: '+g.name);
        const seen=new Set();for(const l of g.links){const a=specs.get(l.from?.node)?.outputs.find(p=>p.name===l.from.pin),b=specs.get(l.to?.node)?.inputs.find(p=>p.name===l.to.pin);if(!a||!b||a.type!==b.type)throw Error('Invalid macro link '+g.name);const k=a.type==='exec'?'o:'+l.from.node+':'+l.from.pin:'i:'+l.to.node+':'+l.to.pin;if(seen.has(k))throw Error('Duplicate macro connection '+g.name);seen.add(k);}
        for(const exec of [true,false]){const active=new Set(),done=new Set();function visit(id){if(active.has(id))throw Error('Macro '+(exec?'execution':'data')+' cycle '+g.name);if(done.has(id))return;active.add(id);for(const l of g.links)if(l.from.node===id&&((specs.get(id).outputs.find(p=>p.name===l.from.pin).type==='exec')===exec))visit(l.to.node);active.delete(id);done.add(id);}for(const id of specs.keys())visit(id);}
    }
    function expandMacros(doc,original){
        if(!original.nodes?.some(n=>n.kind==='macroCall'))return original;
        const g=JSON.parse(JSON.stringify(original)), used=new Set(g.nodes.map(n=>n.id));let serial=0, expansions=0;
        const id=()=>{let x;do{x='bpMacro'+serial++;}while(used.has(x));used.add(x);return x;};
        function expand(call,stack){
            const macro=members(doc,'graphs').find(m=>m.kind==='macro'&&m.name===call.ref);if(!macro||stack.includes(macro.name)||stack.length>=16||++expansions>128)throw Error('Invalid or recursive macro '+call.ref);validateMacro(doc,macro);
            const spec=describe(doc,g,call), entry=macro.nodes.find(n=>n.kind==='entry'), mapping=new Map();
            const oldLinks=g.links.slice(), inLinks=oldLinks.filter(l=>l.to.node===call.id), outLinks=oldLinks.filter(l=>l.from.node===call.id);
            for(const l of inLinks){const p=spec.inputs.find(x=>x.name===l.to.pin);if(!p)throw Error('Unknown macro input '+l.to.pin);const n=g.nodes.find(n=>n.id===l.from.node);if(!n||describe(doc,g,n)?.outputs.find(x=>x.name===l.from.pin)?.type!==p.type)throw Error('Incompatible macro input '+l.to.pin);}
            for(const l of outLinks){const p=spec.outputs.find(x=>x.name===l.from.pin),n=g.nodes.find(n=>n.id===l.to.node);if(!p||!n||describe(doc,g,n)?.inputs.find(x=>x.name===l.to.pin)?.type!==p.type)throw Error('Incompatible macro output '+l.from.pin);}
            if(inLinks.filter(l=>l.to.pin!=='in').some((l,i,a)=>a.findIndex(x=>x.to.pin===l.to.pin)!==i)||outLinks.filter(l=>spec.outputs.find(p=>p.name===l.from.pin)?.type==='exec').some((l,i,a)=>a.findIndex(x=>x.from.pin===l.from.pin)!==i))throw Error('Duplicate macro call connection');
            for(const p of macro.params)if(Object.hasOwn(call.defaults||{},p.name))literal(p.type,call.defaults[p.name]);
            g.links=oldLinks.filter(l=>l.from.node!==call.id&&l.to.node!==call.id);g.links.push(...inLinks.filter(l=>l.to.pin==='in'));
            const proxy={...call,kind:'macroEnter'};delete proxy.ref;delete proxy.defaults;g.nodes[g.nodes.indexOf(call)]=proxy;
            const nested=[];for(const n of macro.nodes){if(n.kind==='entry'){mapping.set(n.id,call.id);continue;}const next={...JSON.parse(JSON.stringify(n)),id:id()};if(n.kind==='macroExit'){next.kind='macroEnter';delete next.ref;delete next.defaults;}mapping.set(n.id,next.id);g.nodes.push(next);if(n.kind==='macroCall')nested.push(next);}
            const inputSource=new Map();function constant(p,value){const n={id:id(),kind:'constant',type:p.type,defaults:{value}};g.nodes.push(n);return {node:n.id,pin:'value'};}
            for(const p of macro.params){const l=inLinks.find(l=>l.to.pin===p.name);inputSource.set(p.name,l?l.from:constant(p,Object.hasOwn(call.defaults||{},p.name)?call.defaults[p.name]:defaultValue(p.type)));}
            function source(l){if(l.from.node===entry.id&&l.from.pin!=='then')return inputSource.get(l.from.pin);return {node:mapping.get(l.from.node),pin:l.from.pin};}
            for(const l of macro.links){const exit=macro.nodes.find(n=>n.id===l.to.node&&n.kind==='macroExit');if(exit&&l.to.pin!=='in')continue;g.links.push({from:source(l),to:{node:mapping.get(l.to.node),pin:l.to.pin}});}
            for(const l of outLinks){if(macro.exits.includes(l.from.pin)){const e=macro.nodes.find(n=>n.kind==='macroExit'&&n.ref===l.from.pin);g.links.push({from:{node:mapping.get(e.id),pin:'then'},to:l.to});}else{const e=macro.nodes.find(n=>n.kind==='macroExit'),p=macro.results.find(p=>p.name===l.from.pin),link=macro.links.find(x=>x.to.node===e.id&&x.to.pin===p.name);const from=link?source(link):constant(p,Object.hasOwn(e.defaults||{},p.name)?e.defaults[p.name]:defaultValue(p.type));g.links.push({from,to:l.to});}}
            if(g.nodes.length>2048||g.links.length>8192)throw Error('Macro expansion exceeds limits');
            for(const n of nested)expand(n,[...stack,macro.name]);
        }
        for(const n of [...g.nodes])if(n.kind==='macroCall')expand(n,[]);return g;
    }
    // Find token boundaries without interpreting handwritten code. Native AngelScript checks its syntax.
    function scriptTokens(source) {
        if(typeof source!=='string'||new TextEncoder().encode(source).length>256*1024||source.includes('\0'))throw Error('Script must be UTF-8 text of at most 256 KiB');
        const pattern=/\s+|\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"""[\s\S]*?"""|"(?:[^"\\\r\n]|\\[^\r\n])*"|'(?:[^'\\\r\n]|\\[^\r\n])*'|(?:\d+\.\d+|\d+)(?:[eE][+-]?\d+)?[fFuU]?|[A-Za-z_]\w*|::|==|!=|<=|>=|&&|\|\||\+=|-=|\*=|\/=|[^\s]/y;
        const out=[];let offset=0,line=1;
        while(offset<source.length){pattern.lastIndex=offset;const m=pattern.exec(source);if(!m)throw Error('Line '+line+': cannot read source');const v=m[0];
            if((source.startsWith('/*',offset)&&!v.startsWith('/*'))||(source.startsWith('"""',offset)&&!v.startsWith('"""'))||v==='"'||v==="'")throw Error('Line '+line+': unclosed comment or string');
            if(!/^\s|^\/\/|^\/\*/.test(v))out.push({v,line,start:offset,end:pattern.lastIndex});
            line+=(v.match(/\n/g)||[]).length;offset=pattern.lastIndex;if(out.length>24000)throw Error('Script has too many tokens');
        }return out;
    }
    function validateScriptBody(body){
        const tokens=scriptTokens(body);let depth=0;
        for(const t of tokens){if(t.v==='#')throw Error('Preprocessor directives are not supported inside handwritten functions');if(t.v==='{')depth++;if(t.v==='}'&&--depth<0)throw Error('Handwritten function closes outside its body');if(depth>64)throw Error('Handwritten function nesting exceeds limit');}
        if(depth!==0)throw Error('Unbalanced braces in handwritten function');return true;
    }
    function scriptContract(doc){return {name:doc.name,kind:doc.kind,variables:doc.variables.map(v=>({name:v.name,type:v.type})),graphs:doc.graphs.filter(g=>g.kind!=='macro').map(g=>({name:g.name,kind:g.kind,returns:g.returns,params:g.params||[],pure:!!g.pure}))};}
    function checkScriptContract(doc){
        const contract=doc.importedScript?.contract;
        let bytes=0;for(const g of doc.graphs)if(g.handwritten){if(!['event','function'].includes(g.kind)||typeof g.handwritten.body!=='string')throw Error('Invalid handwritten method');bytes+=new TextEncoder().encode(g.handwritten.body).length;if(bytes>256*1024)throw Error('Combined handwritten bodies exceed 256 KiB');}
        if(!contract){if(doc.graphs.some(g=>g.handwritten))throw Error('Handwritten functions need their imported class contract');return;}
        if(contract.name!==doc.name||contract.kind!==doc.kind||doc.parent||!Array.isArray(contract.variables)||!Array.isArray(contract.graphs))throw Error('Keep the imported class name and base while handwritten functions remain');
        const now=scriptContract(doc);for(const v of contract.variables)if(!now.variables.some(x=>x.name===v.name&&x.type===v.type))throw Error('Keep imported variable '+v.name+' while handwritten functions remain');
        for(const g of contract.graphs)if(!now.graphs.some(x=>JSON.stringify(x)===JSON.stringify(g)))throw Error('Keep imported function signature '+g.name+' while handwritten functions remain');
    }
    function compile(doc, nested = false, options = {}) {
        const diagnostics = [];
        const fail = (message, graph = '', node = '') => { diagnostics.push({ message, graph, node }); throw Error(message); };
        let current = '', node = '';
        try {
            if (!doc || doc.version !== 1 || !Object.hasOwn(events, doc.kind) || !ident(doc.name) || !Array.isArray(doc.variables) || !Array.isArray(doc.graphs))
                fail('Invalid blueprint: expected version 1, Screen/Behaviour, class name, variables and graphs');
            if (doc.variables.length > 128 || doc.graphs.length > 64)
                fail('Blueprint exceeds size limits');
            checkScriptContract(doc);
            const chain=lineage(doc);
            validateHierarchies(doc);
            const emitInterfaces = !options.interfaceRegistry;
            const registry=options.interfaceRegistry || interfaceRegistry(doc);
            const levelNeeded=doc.kind==='Screen'&&(options.levelNeeded||chain.some(c=>c.graphs.some(g=>g.nodes.some(n=>levelNodes.has(n.kind)))));
            options={...options,interfaceRegistry:registry,levelNeeded};
            const implementations=(doc.interfaces||[]).filter(i=>i.implement);
            for(const i of implementations) for(const m of i.methods){
                const f=members(doc,'graphs').find(g=>g.kind==='function'&&g.name===m.name);
                if(!f||f.returns!==m.returns||f.params?.length!==m.params.length||f.params.some((p,j)=>p.type!==m.params[j].type)) fail('Interface '+i.name+' requires matching function '+m.name);
            }
            const inheritedInterfaces=new Set(chain.slice(0,-1).flatMap(d=>(d.interfaces||[]).filter(i=>i.implement).map(i=>i.name)));
            const interfaceBases=implementations.filter(i=>!inheritedInterfaces.has(i.name)).map(i=>'bp_iface_'+i.name);
            const inheritedVars=doc.parent?members(doc.parent,'variables'):[];
            const inheritedGraphs=doc.parent?members(doc.parent,'graphs'):[];
            const names = new Set([...Object.keys(events[doc.kind]),...inheritedVars.map(v=>v.name)]);
            // Base function names are reserved for matching-signature overrides, not variables.
            for(const v of doc.variables) if(inheritedGraphs.some(g=>g.name===v.name)) fail('Variable conflicts with inherited graph '+v.name);
            if (doc.kind === 'Behaviour')
                names.add('entity');
            names.add(doc.name);
            for (const v of doc.variables) {
                if (!ident(v.name) || names.has(v.name) || !types.includes(v.type))
                    fail('Invalid or duplicate variable ' + v.name);
                names.add(v.name);
                literal(v.type, v.default);
            }
            const graphNames = new Set();
            for (const g of doc.graphs) {
                if (!ident(g.name) || graphNames.has(g.name) || !['event', 'function','macro'].includes(g.kind))
                    fail('Invalid or duplicate graph ' + g.name);
                graphNames.add(g.name);
                if (g.kind === 'event' && !Object.hasOwn(events[doc.kind], g.name))
                    fail('Unknown event ' + g.name);
                if(g.kind==='macro'){
                    if(names.has(g.name)||inheritedGraphs.some(x=>x.name===g.name))fail('Invalid or duplicate macro '+g.name);
                    names.add(g.name); validateMacro(doc,g);
                }
                if (g.kind === 'function') {
                    if (names.has(g.name) || !Array.isArray(g.params) || g.params.length > 24 || !['void', ...types].includes(g.returns))
                        fail('Invalid function ' + g.name);
                    const base=inheritedGraphs.find(x=>x.name===g.name);
                    if(base && (base.kind!=='function'||base.returns!==g.returns||!!base.pure!==!!g.pure||base.params.length!==g.params.length||base.params.some((p,i)=>p.type!==g.params[i].type||p.name!==g.params[i].name))) fail('Override must preserve parent function signature '+g.name);
                    if(g.pure && g.returns==='void')fail('Pure functions must return a value',g.name);
                    names.add(g.name);
                    const pnames = new Set();
                    for (const p of g.params) {
                        if (!ident(p.name) || pnames.has(p.name) || p.name === 'then' || p.name === 'in' || !types.includes(p.type))
                            fail('Invalid parameter ' + p.name);
                        pnames.add(p.name);
                    }
                }
            }
            const macros=members(doc,'graphs').filter(g=>g.kind==='macro'), visiting=new Set(), checked=new Set();
            function macroCycles(g){if(visiting.has(g.name))fail('Recursive macro '+g.name);if(checked.has(g.name))return;visiting.add(g.name);for(const n of g.nodes.filter(n=>n.kind==='macroCall')){const next=macros.find(m=>m.name===n.ref);if(!next)fail('Unknown macro '+n.ref);macroCycles(next);}visiting.delete(g.name);checked.add(g.name);}
            for(const g of macros)macroCycles(g);
            const dispatchers = doc.dispatchers || [];
            if (!Array.isArray(dispatchers) || dispatchers.length > 32) fail('Invalid dispatcher list');
            for (const d of dispatchers) {
                if (!ident(d.name) || names.has(d.name) || !Array.isArray(d.params) || d.params.length > 24) fail('Invalid or duplicate dispatcher ' + d.name);
                names.add(d.name); const seen = new Set(['in','then']);
                for (const p of d.params) { if (!ident(p.name) || seen.has(p.name) || !types.includes(p.type)) fail('Invalid dispatcher parameter'); seen.add(p.name); }
            }
            const componentSources = [];
            if(doc.parent){
                const r=compile(doc.parent,nested,{...options,abstract:true});
                if(!r.ok){for(const d of r.diagnostics) diagnostics.push({...d,graph:doc.parent.name+'/'+d.graph}); fail('Parent Blueprint did not compile');}
                componentSources.push(r.source);
                // Prevent duplicate declarations of embedded component classes across the chain.
                const classes=new Set(chain.map(c=>c.name));
                for(const c of chain) for(const b of c.components||[]){if(classes.has(b.name))fail('Duplicate class across inheritance: '+b.name);classes.add(b.name);}
            }
            if (doc.components !== undefined) {
                if (nested || doc.kind !== 'Screen' || !Array.isArray(doc.components) || doc.components.length > 24)
                    fail('Only Screen blueprints can import up to 24 Behaviour components');
                const classNames = new Set([doc.name]);
                for (const component of doc.components) {
                    if (!component || component.kind !== 'Behaviour' || classNames.has(component.name))
                        fail('Invalid or duplicate Behaviour component');
                    classNames.add(component.name);
                    const r = compile(component, true, {...options,abstract:false});
                    for (const d of r.diagnostics)
                        diagnostics.push({ ...d, graph: component.name + '/' + d.graph });
                    if (!r.ok)
                        fail('Component ' + component.name + ' did not compile');
                    componentSources.push(r.source);
                }
            }
            const lines = ['// Generated by Nexgen Blueprint v1. Edit the .bp.json graph, not this file.', (options.abstract?'abstract class ':'class ') + doc.name + ' : ' + (doc.parent?.name || doc.kind) + (interfaceBases.length?', '+interfaceBases.join(', '):''), '{'];
            const push = (s, depth = 1) => { if (lines.length > 20000)
                fail('Expanded graph too large', current, node); lines.push('    '.repeat(depth) + s); };
            const fields = [];
            if(levelNeeded&&!doc.parent)push('protected Level bp_nativeLevel;');
            const statePrefix=options.abstract||doc.parent?'bp_'+doc.name+'_':'bp_';
            const tickName=statePrefix+'tickDelays';
            const timers = [], timelines = [];
            const dispatchFields = new Map();
            for(const d of dispatchers) {
                const bound = new Set(doc.graphs.flatMap(g=>g.nodes || []).filter(n=>n.kind==='bind' && n.ref===d.name).map(n=>n.handler));
                const handlers = doc.graphs.filter(f=>bound.has(f.name) && compatibleHandler(f,d));
                dispatchFields.set(d.name, handlers.map((f,i)=>({name:f.name, field:statePrefix+'dispatch_'+d.name+'_'+i})));
                for(const h of dispatchFields.get(d.name)) fields.push('bool '+h.field+' = false;');
            }
            const latentMethods = [];
            let serial = 0;
            const calls = new Map();
            const methods = [];
            for (const v of doc.variables)
                push(v.type + ' ' + v.name + ' = ' + literal(v.type, v.default) + ';');
            for (const original of doc.graphs) {
                if(original.kind==='macro')continue;
                current=original.name;
                if(original.handwritten){
                    const raw=original.handwritten, expected=original.kind==='event'?events[doc.kind][original.name].map(([name,type])=>({name,type})):original.params;
                    if(original.pure||original.nodes?.length!==1||original.nodes[0].kind!=='entry'||original.links?.length!==0||original.locals?.length||!Array.isArray(raw.params)||raw.params.length!==expected.length||raw.params.some((p,i)=>!ident(p.name)||p.type!==expected[i].type||original.kind==='function'&&p.name!==expected[i].name)||new Set(raw.params.map(p=>p.name)).size!==raw.params.length)fail('Invalid handwritten function or unexpected graph nodes',current);
                    validateScriptBody(raw.body);
                    diagnostics.push({severity:'warning',code:'handwritten',message:'Handwritten AngelScript needs native compilation',graph:current,node:original.nodes[0].id});
                    methods.push('    '+(original.kind==='event'?'void':original.returns)+' '+original.name+'('+raw.params.map(p=>p.type+' '+p.name).join(', ')+')','    {',raw.body,'    }');
                    continue;
                }
                const g=expandMacros(doc,original);
                current = g.name;
                node = '';
                if (!Array.isArray(g.nodes) || !Array.isArray(g.links) || g.nodes.length > 512 || g.links.length > 2048)
                    fail('Invalid graph or graph too large', current);
                if(g.locals!==undefined){if(g.kind!=='function'||!Array.isArray(g.locals)||g.locals.length>64)fail('Invalid local variable list',current);const seen=new Set();for(const v of g.locals){if(!ident(v.name)||seen.has(v.name)||!types.includes(v.type))fail('Invalid local variable '+v.name,current);seen.add(v.name);literal(v.type,v.default);}}
                const nodes = new Map(), ds = new Map(), incoming = new Map(), outgoing = new Map();
                const key = (id, p) => id + '|' + p;
                for (const n of g.nodes) {
                    node = n.id;
                    if (typeof n.id !== 'string' || !/^[A-Za-z0-9_-]{1,64}$/.test(n.id) || nodes.has(n.id))
                        fail('Invalid or duplicate node ID', current, node);
                    const d = describe(doc, g, n);
                    if (!d)
                        fail('Unknown or invalid node ' + n.kind, current, node);
                    if(n.kind==='switch'){const seen=new Set();for(const value of n.cases){const str=literal(n.type,value);if(seen.has(str))fail('Duplicate switch case',current,n.id);seen.add(str);}}
                    if(n.kind==='timeline')validateTimeline(n);if(n.kind==='stateMachine')validateMachine(n);
                    if(g.pure && n.kind!=='entry'&&n.kind!=='return'&&n.kind!=='comment'&&d.inputs.some(p=>p.type==='exec'))fail('Pure functions cannot contain execution/side-effect nodes',current,n.id);
                    nodes.set(n.id, n);
                    ds.set(n.id, d);
                    for (const p of d.inputs.filter(p => p.type !== 'exec'))
                        if (n.defaults && Object.hasOwn(n.defaults, p.name))
                            literal(p.type, n.defaults[p.name]);
                }
                const entries = g.nodes.filter(n => n.kind === 'entry');
                if (entries.length !== 1)
                    fail('Graph needs exactly one entry', current);
                for (const l of g.links) {
                    const a = ds.get(l.from?.node)?.outputs.find(p => p.name === l.from.pin), b = ds.get(l.to?.node)?.inputs.find(p => p.name === l.to.pin);
                    if (!a || !b || a.type !== b.type)
                        fail('Invalid or incompatible link', current, l.to?.node);
                    const dest = key(l.to.node, l.to.pin), src = key(l.from.node, l.from.pin);
                    if (b.type === 'exec') {
                        if (outgoing.has(src))
                            fail('Execution output may connect once (use Sequence)', current, l.from.node);
                        outgoing.set(src, l.to.node);
                    }
                    else {
                        if (incoming.has(dest))
                            fail('Data input may have only one connection', current, l.to.node);
                        incoming.set(dest, l.from);
                    }
                }
                // Validate even disconnected data and execution cycles.
                function checkCycles(edges, label) { const active = new Set(), done = new Set(); function visit(id) { if (active.has(id))
                    fail(label + ' cycle (use a bounded loop)', current, id); if (done.has(id))
                    return; active.add(id); for (const next of edges(id))
                    visit(next); active.delete(id); done.add(id); } for (const id of nodes.keys())
                    visit(id); }
                checkCycles(id => ds.get(id).outputs.filter(p => p.type === 'exec').map(p => outgoing.get(key(id, p.name))).filter(Boolean), 'Execution');
                checkCycles(id => ds.get(id).inputs.filter(p => p.type !== 'exec').map(p => incoming.get(key(id, p.name))?.node).filter(Boolean), 'Data');
                const parameters = g.kind === 'event' ? events[doc.kind][g.name] : g.params.map(p => [p.name, p.type]);
                const ret = g.kind === 'event' ? 'void' : g.returns;
                const entry = entries[0];
                const methodStart = lines.length;
                push(ret + ' ' + g.name + '(' + parameters.map(([n, t]) => t + ' bp_arg_' + n).join(', ') + ')');
                push('{');
                for(const v of g.locals||[])push(v.type+' bp_local_'+v.name+' = '+literal(v.type,v.default)+';',2);
                let loopDepth=0;
                const delays = new Map();
                let latent = false;
                const states = new Map();
                const stateField = id => { if (!states.has(id)) {
                    const f = statePrefix+'state_' + serial++;
                    states.set(id, f);
                    fields.push('bool ' + f + ' = false;');
                } return states.get(id); };
                const visited = new Set([entry.id]);
                const called = new Set();
                calls.set(g.name, called);
                let budget = 0;
                function exprInput(n, p, env, stack = []) { const link = incoming.get(key(n.id, p.name)); if (link)
                    return expr(link.node, link.pin, env, stack); if(n.kind==='levelHud'&&p.name==='canvas')fail('Connect the Draw event canvas',current,n.id); if (p.name === 'target' && (p.type.endsWith('@') || p.type === 'Voice' || p.type === 'Tween'))
                    fail('Connect the target object', current, n.id); return literal(p.type, n.defaults && Object.hasOwn(n.defaults, p.name) ? n.defaults[p.name] : p.default); }
                function apiExpr(a, args) { const target = a.member ? '(' + args.shift() + ').' : '', name = a.member ? a.call.split('::').pop() : a.call; if (a.property) {
                    const prop = name.replace(/(^|::)(get_|set_)/, '$1');
                    return target + prop + (a.property === 'set' ? ' = ' + args[0] : '');
                } return target + name + '(' + args.join(', ') + ')'; }
                function expr(id, p, env, stack) { if (++budget > 20000)
                    fail('Expanded graph too large', current, id); if (stack.includes(id) || stack.length > 128)
                    fail('Data cycle or excessive depth', current, id); visited.add(id); const n = nodes.get(id), d = ds.get(id), k = n.kind; const available = env.get(key(id, p)); if (available)
                    return available; if (k === 'entry') {
                    if (latent)
                        fail('Event arguments cannot cross Delay; store them in instance variables before waiting', current, id);
                    return 'bp_arg_' + p;
                } if (k === 'self')
                    return 'entity'; if (k === 'get')
                    return 'this.' + n.ref; if(k==='getLocal')return 'bp_local_'+n.ref; if (d.inputs.some(p => p.type === 'exec'))
                    fail('Data from an impure node is only available after it executes on this path', current, id); const a = d.inputs.map(p => exprInput(n, p, env, [...stack, id])); if (k === 'constant' || k === 'reroute')
                    return a[0]; if(k==='clamp') return '('+a[0]+' < '+a[1]+' ? '+a[1]+' : ('+a[0]+' > '+a[2]+' ? '+a[2]+' : '+a[0]+'))'; if(k==='lerp') return '('+a[0]+' + ('+a[1]+' - '+a[0]+') * '+a[2]+')'; if(k==='parentCall')return doc.parent.name+'::'+g.name+'('+a.join(', ')+')';if(k==='call'){called.add(n.ref);return 'this.'+n.ref+'('+a.join(', ')+')';} if (k === 'arrayLength')
                    return '(' + a[0] + ').length()'; if (k === 'arrayGet')
                    return '(' + a[0] + ')[' + a[1] + ']'; if (api.has(k))
                    return '(' + apiExpr(api.get(k), a) + ')'; if (Object.hasOwn(maths, k))
                    return '(' + a[0] + ' ' + maths[k] + ' ' + a[1] + ')'; if (k === 'not')
                    return '(!' + a[0] + ')'; if (k === 'valid')
                    return '(' + a[0] + ' !is null)'; if (k === 'toString')
                    return '("" + ' + a[0] + ')'; if (k === 'convert')
                    return n.to + '(' + a[0] + ')'; if (k === 'select')
                    return '(' + a[0] + ' ? ' + a[1] + ' : ' + a[2] + ')'; if (k === 'vector')
                    return n.type + '(' + a.join(', ') + ')'; if (k === 'component')
                    return '(' + a[0] + ').' + n.ref; fail('No data output', current, id); }
                function flow(id, env, depth) {
                    if (!id)
                        return false;
                    if (++budget > 20000 || depth > 130)
                        fail('Expanded graph too large or deep', current, id);
                    visited.add(id);
                    node = id;
                    const n = nodes.get(id), d = ds.get(id), k = n.kind;
                    const a = d.inputs.filter(p => p.type !== 'exec').map(p => exprInput(n, p, env));
                    const go = (p, e = env, dep = depth) => flow(outgoing.get(key(id, p)), e, dep);
                    const line = s => push(s, depth);
                    line('// bp-node: ' + id + ' ' + k);
                    if(options.trace) line('print('+literal('string','[BP] '+doc.name+'/'+g.name+'/'+id)+');');
                    if(k==='hierarchySpawn'){
                        const h=doc.hierarchies.find(h=>h.name===n.ref), order=hierarchyOrder(doc,h),three=h.mode==='3D',vec=three?'Vec3':'Vec2',origin='bp_origin_'+serial++,ok='bp_spawn_ok_'+serial++,refs=new Map();
                        line(vec+' '+origin+' = '+a[0]+';');line('bool '+ok+' = true;');
                        for(const st of order){const ent='bp_entity_'+serial++;refs.set(st.name,ent);line('Entity@ '+ent+' = null;');}
                        for(const st of order){
                            const ent=refs.get(st.name), pos=literal(vec,st.position), rot=literal(three?'Vec3':'float',st.rotation),scale=literal(vec,st.scale);
                            line('if ('+ok+') {');
                            push('@'+ent+' = '+(three?'Space':'Scene')+'::spawn('+(st.parent===null?origin+' + ':'')+pos+');',depth+1);
                            push(ok+' = '+ent+' !is null;',depth+1);
                            push('if ('+ok+') {',depth+1);
                            push(ent+'.name = '+literal('string',st.name)+';',depth+2);
                            if(st.parent!==null){push(ent+'.attach('+refs.get(st.parent)+');',depth+2);push(ent+'.'+(three?'place':'position')+' = '+pos+';',depth+2);}
                            push(ent+'.'+(three?'angles':'rotation')+' = '+rot+';',depth+2);push(ent+'.'+(three?'size':'scale')+' = '+scale+';',depth+2);
                            const v=st.visual||{kind:'none'};
                            if(v.kind==='rect')push(ent+'.rect('+[...v.size.map(x=>literal('float',x)),literal('Color',v.color)].join(', ')+');',depth+2);
                            if(v.kind==='disc')push(ent+'.disc('+literal('float',v.radius)+', '+literal('Color',v.color)+');',depth+2);
                            if(v.kind==='shape')push(ent+'.shape('+literal('Solid',v.shape)+', '+literal('Color',v.color)+');',depth+2);
                            if(v.kind==='model')push(ent+'.model(Assets::model('+literal('string',v.asset)+'));',depth+2);
                            if(v.kind==='image')push(ent+'.image(Assets::texture('+literal('string',v.asset)+'), '+v.size.map(x=>literal('float',x)).join(', ')+');',depth+2);
                            push('}',depth+1);line('}');
                        }
                        line('if ('+ok+') {');
                        const e=new Map(env);for(const st of order){const ent=refs.get(st.name);e.set(key(id,'entity_'+st.name),ent);if(st.parent===null)e.set(key(id,'root'),ent);for(const c of st.behaviours)push(ent+'.add('+c+'());',depth+1);}
                        const yes=go('then',e,depth+1);line('} else {');
                        for(const st of order.slice().reverse()){const ent=refs.get(st.name);push('if ('+ent+' !is null && '+ent+'.alive) '+ent+'.destroy();',depth+1);}
                        const no=go('failed',new Map(env),depth+1);line('}');return yes&&no;
                    }
                    if(k==='stateMachine'){
                        const mode=n.mode||'logic',offset=mode==='logic'?0:1;
                        let f=states.get(id);if(!f){f=statePrefix+'machine_'+serial++;states.set(id,f);fields.push('int '+f+'_state = -1;','float '+f+'_elapsed = 0.0f;');if(mode!=='logic')fields.push('Entity@ '+f+'_target = null;','bool '+f+'_started = false;');}
                        const changed='bp_entered_'+serial++,active='bp_state_'+serial++,elapsed='bp_elapsed_'+serial++;
                        const stateIndex=name=>n.states.findIndex(s=>s.name===name);
                        line('bool '+changed+' = false;');
                        line('if ('+f+'_state < 0 || ('+a[offset+1]+')) {');
                        push(f+'_state = '+stateIndex(n.initial)+';',depth+1);push(changed+' = true;',depth+1);line('} else {');
                        n.transitions.forEach((t,i)=>{const to=stateIndex(t.to),from=t.from==='*'?f+'_state != '+to:f+'_state == '+stateIndex(t.from);push((i?'else if':'if')+' ('+from+' && ('+a[offset+2+i]+')) { '+f+'_state = '+to+'; '+changed+' = true; }',depth+1);});line('}');
                        line('if ('+changed+') '+f+'_elapsed = 0.0f; else '+f+'_elapsed += '+a[offset]+';');
                        line('float '+elapsed+' = '+f+'_elapsed;');
                        line('string '+active+' = "";');
                        const e=new Map(env);e.set(key(id,'activeState'),active);e.set(key(id,'entered'),changed);e.set(key(id,'elapsed'),elapsed);
                        let target;if(mode!=='logic'){target='bp_animation_'+serial++;line('Entity@ '+target+' = '+a[0]+';');line('if ('+changed+' || '+target+' !is '+f+'_target) { @'+f+'_target = '+target+'; '+f+'_started = false; }');}
                        n.states.forEach((st,i)=>{
                            line((i?'else if':'if')+' ('+f+'_state == '+i+') {');push(active+' = '+literal('string',st.name)+';',depth+1);
                            if(mode!=='logic'){
                                push('if (!'+f+'_started && '+target+' !is null && '+target+'.alive) {',depth+1);
                                if(mode==='sprite'){push(target+'.play('+[literal('uint',st.first),literal('uint',st.last),literal('float',st.fps),literal('bool',st.loop)].join(', ')+');',depth+2);push(f+'_started = true;',depth+2);}
                                else {push(f+'_started = '+target+'.animate('+[literal('string',st.clip),literal('bool',st.loop),literal('float',st.blend)].join(', ')+');',depth+2);push('if ('+f+'_started) '+target+'.animationSpeed = '+literal('float',st.speed)+';',depth+2);}
                                push('}',depth+1);
                            }
                            go('state_'+st.name,new Map(e),depth+1);line('}');
                        });
                        return go('then',e);
                    }
                    if(k==='interfaceMessage') {
                        const m=interfaceMethod(doc,n), entity='bp_target_'+serial++, target='bp_receiver_'+serial++, idx='bp_index_'+serial++, item='bp_behaviour_'+serial++;
                        line('Entity@ '+entity+' = '+a[0]+';');
                        line('bp_iface_'+n.ref+'@ '+target+' = null;');
                        line('if ('+entity+' !is null && '+entity+'.alive) {');
                        push('for (uint '+idx+' = 0; ; ++'+idx+') {',depth+1);
                        push('IBehaviour@ '+item+' = '+entity+'.behaviourAt('+idx+');',depth+2);
                        push('if ('+item+' is null) break;',depth+2);
                        push('@'+target+' = cast<bp_iface_'+n.ref+'>('+item+');',depth+2);
                        push('if ('+target+' !is null) break;',depth+2);
                        push('}',depth+1);line('}');
                        line('if ('+target+' !is null) {');
                        const e=new Map(env), statement=target+'.'+m.name+'('+a.slice(1).join(', ')+')';
                        if(m.returns==='void') push(statement+';',depth+1);
                        else {const value='bp_message_'+serial++;push(m.returns+' '+value+' = '+statement+';',depth+1);e.set(key(id,'result'),value);}
                        const yes=go('then',e,depth+1);line('} else {');const no=go('missing',new Map(env),depth+1);line('}');return yes&&no;
                    }
                    if(k==='macroEnter') return go('then');
                    if(k==='breakLoop'||k==='continueLoop'){if(!loopDepth||latent)fail('Loop control must run inside an active loop body',current,id);line(k==='breakLoop'?'break;':'continue;');return true;}
                    if(k==='while'){const f='bp_loop_'+serial++;line('for (int '+f+' = 0; '+f+' < 10000 && ('+a[0]+'); ++'+f+') {');loopDepth++;go('body',new Map(env),depth+1);loopDepth--;line('}');return go('completed');}
                    if(k==='forEach'){const f='bp_loop_'+serial++,arr='bp_array_'+serial++,value='bp_element_'+serial++;line(n.type+' '+arr+' = '+a[0]+';');line('if ('+arr+' !is null) {');push('for (uint '+f+' = 0; '+f+' < '+arr+'.length() && '+f+' < 10000; ++'+f+') {',depth+1);push(n.type.slice(6,-2)+' '+value+' = '+arr+'['+f+'];',depth+2);const e=new Map(env);e.set(key(id,'index'),f);e.set(key(id,'element'),value);loopDepth++;go('body',e,depth+2);loopDepth--;push('}',depth+1);line('}');return go('completed');}
                    if(k==='switch'){const f='bp_switch_'+serial++;line(n.type+' '+f+' = '+a[0]+';');let returned=true;n.cases.forEach((value,i)=>{line((i?'else if':'if')+' ('+f+' == '+literal(n.type,value)+') {');const r=go('case'+i,new Map(env),depth+1);returned=returned&&r;line('}');});line('else {');const fallback=go('default',new Map(env),depth+1);line('}');return returned&&fallback;}
                    if(k==='gate') { line('if ('+a[0]+') {'); go('then',new Map(env),depth+1); line('}'); return false; }
                    if(k==='doN') {
                        let f=states.get(id); if(!f){f=statePrefix+'counter_'+serial++; states.set(id,f); fields.push('int '+f+' = 0;');}
                        line('if ('+a[1]+') '+f+' = 0;'); line('if ('+f+' < '+a[0]+') {');
                        push('++'+f+';',depth+1); const e=new Map(env); e.set(key(id,'counter'),f); go('then',e,depth+1); line('}'); return false;
                    }
                    if(['bind','unbind','unbindAll','broadcast'].includes(k)) {
                        const handlers=dispatchFields.get(n.ref);
                        if(k==='broadcast') {
                            const args=a.map((v,i)=>{ const t=d.inputs.filter(p=>p.type!=='exec')[i].type, local='bp_dispatcharg_'+serial++; line(t+' '+local+' = '+v+';'); return local; });
                            // Snapshot membership so changes during a broadcast affect the next one.
                            const flags=handlers.map(h=>{const local='bp_bound_'+serial++; line('bool '+local+' = '+h.field+';'); return local;});
                            handlers.forEach((h,i)=>{called.add(h.name); line('if ('+flags[i]+') this.'+h.name+'('+args.join(', ')+');');});
                        } else for(const h of handlers) if(k==='unbindAll'||h.name===n.handler) line(h.field+' = '+(k==='bind'?'true':'false')+';');
                        return go('then');
                    }
                    if(k==='timeline') {
                        let t=delays.get(id);
                        if(!t){
                            t=statePrefix+'timeline_'+serial++; delays.set(id,t); timelines.push(t);
                            fields.push('bool '+t+'_active = false;', 'float '+t+'_elapsed = 0.0f;', 'float '+t+'_duration = 0.0f;', 'float '+t+'_from = 0.0f;', 'float '+t+'_to = 1.0f;', 'bool '+t+'_smooth = false;');
                            const start=lines.length; push('void '+t+'_step(float dt)'); push('{');
                            push(t+'_elapsed += dt;',2); push('float alpha = '+t+'_duration <= 0.0f ? 1.0f : '+t+'_elapsed / '+t+'_duration;',2); push('if (alpha < 0.0f) alpha = 0.0f;',2); push('if (alpha > 1.0f) alpha = 1.0f;',2);
                            const mode=n.playback||'forward';
                            if(mode==='reverse')push('alpha = 1.0f - alpha;',2);
                            if(mode==='loop'||mode==='pingpong') {
                                // Reduce excess time without unbounded iteration after a large tick.
                                const cycle=mode==='pingpong'?'2.0f':'1.0f';
                                push('if ('+t+'_duration > 0.0f && '+t+'_elapsed >= '+t+'_duration * '+cycle+') '+t+'_elapsed = '+t+'_elapsed % ('+t+'_duration * '+cycle+');',2);
                                push('alpha = '+t+'_duration <= 0.0f ? 1.0f : '+t+'_elapsed / '+t+'_duration;',2);
                                if(mode==='pingpong')push('if (alpha > 1.0f) alpha = 2.0f - alpha;',2);
                            }
                            function curve(keys,name){push('float '+name+' = '+literal('float',keys.at(-1).value)+';',2);for(let i=0;i<keys.length-1;i++){const a=keys[i],b=keys[i+1];push((i?'else if':'if')+' (alpha < '+literal('float',b.time)+') {',2);push('float u = (alpha - '+literal('float',a.time)+') / '+literal('float',b.time-a.time)+';',3);if(a.mode==='smooth')push('u = u*u*(3.0f-2.0f*u);',3);else if(a.mode==='step')push('u = 0.0f;',3);push(name+' = '+literal('float',a.value)+' + ('+literal('float',b.value)+' - '+literal('float',a.value)+') * u;',3);push('}',2);}}
                            if(n.keys)curve(n.keys,'blend');else push('float blend = '+t+'_smooth ? alpha * alpha * (3.0f - 2.0f * alpha) : alpha;',2);
                            push('float value = '+t+'_from + ('+t+'_to - '+t+'_from) * blend;',2);
                            const finish=mode==='reverse'?'alpha <= 0.0f':mode==='forward'?'alpha >= 1.0f':t+'_duration <= 0.0f';
                            push('if ('+finish+') '+t+'_active = false;',2);
                            const tracks=new Map();for(const track of n.tracks||[]){const channels=track.channels.map(keys=>{const field='bp_curve_'+serial++;curve(keys,field);return field;});const value=track.type==='float'?channels[0]:'bp_track_'+serial++;if(track.type!=='float')push(track.type+' '+value+' = '+track.type+'('+channels.join(', ')+');',2);tracks.set(key(id,track.name),value);}
                            const was=latent; latent=true; const e=new Map([[key(id,'value'),'value'],[key(id,'alpha'),'alpha'],...tracks]); go('update',new Map(e),2); push('if ('+finish+') {',2); go('finished',new Map(e),3); push('}',2); latent=was; push('}'); latentMethods.push(...lines.splice(start));
                        }
                        line('if (!'+t+'_active) {'); ['duration','from','to','smooth'].forEach((f,i)=>push(t+'_'+f+' = '+a[i]+';',depth+1)); push(t+'_elapsed = 0.0f;',depth+1); push(t+'_active = true;',depth+1); line('}'); return false;
                    }
                    if (k === 'branch' || k === 'flipflop') {
                        let cond = a[0];
                        if (k === 'flipflop') {
                            const f = stateField(id);
                            line(f + ' = !' + f + ';');
                            cond = f;
                        }
                        line('if (' + cond + ') {');
                        const x = go(k === 'branch' ? 'true' : 'a', new Map(env), depth + 1);
                        line('} else {');
                        const y = go(k === 'branch' ? 'false' : 'b', new Map(env), depth + 1);
                        line('}');
                        return x && y;
                    }
                    if (k === 'sequence') {
                        for (const p of ['first', 'second', 'third'])
                            if (go(p, env))
                                return true;
                        return false;
                    }
                    if (k === 'repeat') {
                        const f = 'bp_loop_' + serial++, count = 'bp_count_' + serial++;
                        line('int ' + count + ' = ' + a[0] + ';');
                        line('if (' + count + ' > 10000) ' + count + ' = 10000;');
                        line('for (int ' + f + ' = 0; ' + f + ' < ' + count + '; ++' + f + ') {');
                        const e = new Map(env);
                        e.set(key(id, 'index'), f);
                        loopDepth++; go('body', e, depth + 1); loopDepth--;
                        line('}');
                        return go('completed');
                    }
                    if (k === 'delay') {
                        let timer = delays.get(id);
                        if (!timer) {
                            timer = statePrefix+'delay_' + serial++;
                            delays.set(id, timer);
                            timers.push(timer);
                            fields.push('bool ' + timer + '_pending = false;', 'float ' + timer + '_remaining = 0.0f;');
                            const start = lines.length;
                            push('void ' + timer + '_resume()');
                            push('{');
                            const was = latent;
                            latent = true;
                            go('then', new Map(), 2);
                            latent = was;
                            push('}');
                            latentMethods.push(...lines.splice(start));
                        }
                        line('if (!' + timer + '_pending) {');
                        push(timer + '_remaining = ' + a[0] + ';', depth + 1);
                        push(timer + '_pending = true;', depth + 1);
                        line('}');
                        return false;
                    }
                    if (k === 'once') {
                        const f = stateField(id);
                        line('if (' + a[0] + ') ' + f + ' = false;');
                        line('if (!' + f + ') {');
                        push(f + ' = true;', depth + 1);
                        go('then', new Map(env), depth + 1);
                        line('}');
                        return false;
                    }
                    if (k === 'return') {
                        line(ret === 'void' ? 'return;' : 'return ' + a[0] + ';');
                        return true;
                    }
                    let statement, result = 'void', output = 'result';
                    if (k === 'set' || k==='setLocal') {
                        result = d.outputs.find(p => p.name === 'value').type;
                        statement = k==='setLocal'?'bp_local_'+n.ref:'this.' + n.ref;
                        line((result.endsWith('@') ? '@' : '') + statement + ' = ' + a[0] + ';');
                        output = 'value';
                    }
                    else if (k === 'attach') {
                        statement = '(' + a[0] + ').add(' + n.ref + '())';
                    }
                    else if (k === 'construct') {
                        result = n.type;
                        statement = n.type.replace(/@$/, '') + '()';
                    }
                    else if (k === 'arrayNew') {
                        result = n.type;
                        const size = 'bp_size_' + serial++;
                        line('uint ' + size + ' = ' + a[0] + ';');
                        line('if (' + size + ' > 65536) ' + size + ' = 65536;');
                        statement = n.type.slice(0, -1) + '(' + size + ')';
                    }
                    else if (k === 'arrayAdd')
                        statement = '(' + a[0] + ').insertLast(' + a[1] + ')';
                    else if (k === 'arrayRemove')
                        statement = '(' + a[0] + ').removeAt(' + a[1] + ')';
                    else if (k === 'arraySet')
                        statement = (n.type.slice(6, -2).endsWith('@') ? '@' : '') + '(' + a[0] + ')[' + a[1] + '] = ' + a[2];
                    else if (k === 'print')
                        statement = 'print(' + a[0] + ')';
                    else if (k === 'call') {
                        called.add(n.ref);
                        statement = 'this.' + n.ref + '(' + a.join(', ') + ')';
                        result = members(doc,'graphs').find(g => g.name === n.ref).returns;
                    }
                    else if(k==='parentCall') { statement=doc.parent.name+'::'+g.name+'('+a.join(', ')+')'; result=ret; }
                    else if (levelNodes.has(k)) {
                        const spec=levelNodes.get(k);result=spec[2];statement='bp_nativeLevel.'+spec[1]+'('+a.join(', ')+')';
                    }
                    else if (api.has(k)) {
                        const spec = api.get(k);
                        result = spec.result;
                        let args = a;
                        if (spec.outputs?.length) {
                            const byName = new Map(spec.inputs.map((p, i) => [p.name, a[i]]));
                            for (const p of spec.outputs) {
                                const local = 'bp_out_' + serial++;
                                line(p.type + ' ' + local + ' = ' + literal(p.type, undefined) + ';');
                                env.set(key(id, p.name), local);
                                byName.set(p.name, local);
                            }
                            args = [...(spec.member ? [byName.get('target')] : []), ...spec.arguments.map(p => byName.get(p.name))];
                        }
                        statement = apiExpr(spec, args);
                        if (spec.property === 'set' && spec.inputs.at(-1).type.endsWith('@'))
                            statement = '@' + statement;
                    }
                    else
                        fail('Invalid execution node', current, id);
                    if (result === 'void')
                        line(statement + ';');
                    else {
                        const local = 'bp_value_' + serial++;
                        line(result + ' ' + local + ' = ' + statement + ';');
                        env.set(key(id, output), local);
                    }
                    return go('then');
                }
                const returned = flow(outgoing.get(key(entry.id, 'then')), new Map(), 2);
                if (ret !== 'void' && !returned)
                    fail('Every function execution path must return a value', current, entry.id);
                push('}');
                methods.push(...lines.splice(methodStart));
                if (methods.length + latentMethods.length > 20000)
                    fail('Blueprint source exceeds size limits', current);
                for (const n of g.nodes)
                    if (!visited.has(n.id) && n.kind!=='comment')
                        diagnostics.push({ severity: 'warning', message: 'Disconnected node is not compiled', graph: current, node: n.id });
            }
            // Reject recursive Blueprint functions, including indirect recursion.
            const active = new Set(), done = new Set();
            function check(g) { if (active.has(g))
                fail('Recursive Blueprint function calls are not supported', g); if (done.has(g))
                return; active.add(g); for (const x of calls.get(g) || [])
                check(x); active.delete(g); done.add(g); }
            for (const g of calls.keys())
                check(g);
            if (timers.length || timelines.length || doc.parent || options.abstract) {
                // The concrete class owns the engine tick. Base latent state continues even when
                // an event is overridden; Call Parent Update executes only the parent's graph.
                if(!options.abstract) {
                    const rawUpdate=doc.graphs.find(g=>g.name==='update')?.handwritten;
                    const dtName=rawUpdate?rawUpdate.params[0].name:'bp_arg_dt';
                    const index = methods.indexOf('    void update(float '+dtName+')');
                    if (index < 0)
                        methods.push('    void update(float bp_arg_dt)', '    {', '        '+tickName+'(bp_arg_dt);', ...(doc.parent && inheritedGraphs.some(g=>g.name==='update') ? ['        '+doc.parent.name+'::update(bp_arg_dt);'] : []), '    }');
                    else
                        methods.splice(index + 2, 0, '        '+tickName+'('+dtName+');');
                }
                latentMethods.push('    '+(options.abstract?'protected':'private')+' void '+tickName+'(float dt)', '    {');
                for (const t of timelines) latentMethods.push('        bool '+t+'_wasActive = '+t+'_active;');
                for (const t of timers)
                    latentMethods.push('        bool ' + t + '_wasPending = ' + t + '_pending;');
                if(doc.parent) latentMethods.push('        bp_'+doc.parent.name+'_tickDelays(dt);');
                for (const t of timers)
                    latentMethods.push('        if (' + t + '_wasPending && ' + t + '_pending) {', '            ' + t + '_remaining -= dt;', '            if (' + t + '_remaining <= 0.0f) {', '                ' + t + '_pending = false;', '                ' + t + '_resume();', '            }', '        }');
                for (const t of timelines) latentMethods.push('        if ('+t+'_wasActive && '+t+'_active) '+t+'_step(dt);');
                latentMethods.push('    }');
            }
            for (const f of fields)
                push('private ' + f);
            lines.push(...methods, ...latentMethods, '}', '');
            const own=lines.join('\n');
            const declarations=emitInterfaces?[...registry.values()].map(i=>'interface bp_iface_'+i.name+' {\n'+i.methods.map(m=>'    '+m.returns+' '+m.name+'('+m.params.map(p=>p.type+' '+p.name).join(', ')+');').join('\n')+'\n}\n'):[];
            return { ok: true, source: [...(emitInterfaces&&levelNeeded?['#include \"global:scene.as\"']:[]),...declarations,...componentSources, own].join('\n'), diagnostics };
        }
        catch (e) {
            if (!diagnostics.some(d => d.severity !== 'warning'))
                diagnostics.push({ message: e.message, graph: current, node });
            return { ok: false, source: '', diagnostics };
        }
    }
    function traceCompile(doc) { return compile(doc,false,{trace:true}); }
    function debugMap(source) {
        const map=[];
        source.split('\n').forEach((line,i)=>{
            const m=line.match(/^\s*print\("\[BP\] ([A-Za-z_][A-Za-z0-9_]*\/[A-Za-z_][A-Za-z0-9_]*\/[A-Za-z0-9_-]+)"\);$/);
            if(m){const [className,graph,node]=m[1].split('/'); map.push({line:i+1,className,graph,node});}
        }); return map;
    }
    const exported = { scriptTokens, validateScriptBody, scriptContract, checkScriptContract, hierarchyOrder, validateHierarchies, interfaceRegistry, validateMachine, validateTimeline, validateKeys, expandMacros, validateMacro, lineage, members, parentGraph, debugMap, traceCompile, compile, describe, palette, events, types, defaultValue, literal, ident, catalog };
    if (typeof module !== 'undefined')
        module.exports = exported;
    else
        root.NexgenBlueprint = exported;
})(globalThis);
