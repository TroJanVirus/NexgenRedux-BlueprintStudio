'use strict';
const B = NexgenBlueprint, $ = id => document.getElementById(id), clone = o => JSON.parse(JSON.stringify(o));
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls)
    e.className = cls; if (text !== undefined)
    e.textContent = text; return e; };
let graphRendering = false;
let doc = clone(NexgenExample), graphId = doc.graphs[0].name, selected = null, view = { x: 40, y: 35, z: 1 }, armed = null, drag = null, space = false, history = [], future = [], last = null, dirty = false, filterPort = null, contextPos = null;
const graph = () => doc.graphs.find(g => g.name === graphId) || doc.graphs[0];
function uid() { return 'n' + Date.now().toString(36) + Math.random().toString(36).slice(2, 7); }
function freshGraph(kind, name, params = [], returns = 'void') { return { kind, name, params, returns, nodes: [{ id: uid(), kind: 'entry', x: 40, y: 60 }], links: [] }; }
function snapshot(extra = {}) { return JSON.stringify({ doc, graphId, ...extra }); }
function remember(extra) { history.push(snapshot(extra)); if (history.length > 80)
    history.shift(); future = []; dirty = true; last = null; $('diagnostics').replaceChildren(el('div', 'empty', 'Graph changed — compile to refresh results.')); $('source').textContent = ''; updateStatus(); }
function restore(s) { const a = JSON.parse(s); if(a.uiFiles)globalThis.NexgenUiHistory?.restore(a.uiFiles); if(a.sharedLibrary)globalThis.NexgenSharedHistory?.restore(a.sharedLibrary,a.doc); doc = globalThis.NexgenSharedHistory?.resolve(a.doc) || a.doc; graphId = a.graphId; selected = null; armed = null; last = null; dirty = true; render(); }
function historyExtras(target) {
    const record=JSON.parse(target), extra={};
    if(record.uiFiles)extra.uiFiles=globalThis.NexgenUiHistory.capture(record.uiFiles.paths);
    if(record.sharedLibrary)extra.sharedLibrary=globalThis.NexgenSharedHistory.capture();
    return extra;
}
function undo() { if (history.length) {
    future.push(snapshot(historyExtras(history.at(-1))));
    restore(history.pop());
} }
function redo() { if (future.length) {
    history.push(snapshot(historyExtras(future.at(-1))));
    restore(future.pop());
} }
function updateStatus() { $('status').textContent = last ? (last.ok ? 'Compiled' : 'Compile failed') : (dirty ? 'Unsaved changes' : 'Ready'); $('undo').disabled = !history.length; $('redo').disabled = !future.length; }
function color(t) { return t === 'exec' ? '#f0f3f8' : t === 'bool' ? '#f36d7c' : t === 'string' ? '#df91e8' : t.endsWith('@') ? '#70aaff' : ['Vec2', 'Vec3', 'Color'].includes(t) ? '#f6c95c' : '#65d8a2'; }
function svg(tag, attrs) { const e = document.createElementNS('http://www.w3.org/2000/svg', tag); for (const [k, v] of Object.entries(attrs))
    e.setAttribute(k, v); return e; }
function render() { renderSidebar(); renderGraph(); renderPalette(); renderDetails(); updateStatus(); }
function selectGraph(name) { graphId = name; selected = null; armed = null; render(); fit(); }
function item(text, fn, small, active = false) { const x = el('div', 'item' + (active ? ' active' : '')); x.append(el('span', '', text)); if (small)
    x.append(el('small', '', small)); x.onclick = fn; return x; }
function renderSidebar() { const c = $('classInfo'); c.replaceChildren(); const b = el('div', 'classbox'); b.append(el('strong', '', doc.name), el('span', '', (doc.parent?.name || doc.kind) + ' · ' + doc.graphs.length + ' graphs')); c.append(b); const parent=$('parentInfo'); parent.replaceChildren(); if(doc.parent){parent.append(item(doc.parent.name,()=>{selected={parent:true};renderDetails();},'Inherited'));}else parent.append(el('div','empty','Engine base: '+doc.kind));
    const intf=$('interfaces');intf.replaceChildren();
    try {for(const i of B.interfaceRegistry(doc).values()){const local=(doc.interfaces||[]).find(x=>x.name===i.name);intf.append(item(i.name,()=>{selected={interface:i.name};renderDetails();},local?.implement?'Implements':local?'Reference':'Embedded'));}if(!intf.childNodes.length)intf.append(el('div','empty','No interfaces'));}catch(e){intf.append(el('div','empty',e.message));}
    const disp=$('dispatchers');disp.replaceChildren(); for(const d of doc.dispatchers||[])disp.append(item(d.name,()=>{selected={dispatcher:d.name};renderDetails();},d.params.map(p=>p.type).join(', '))); if(!doc.dispatchers?.length)disp.append(el('div','empty','No dispatchers'));
    for (const [k, id] of [['event', 'events'], ['function', 'functions'], ['macro','macros']]) {
    $(id).replaceChildren();
    for (const g of doc.graphs.filter(g => g.kind === k))
        $(id).append(item((k === 'event' ? '◆ ' : 'ƒ ') + g.name, () => selectGraph(g.name), B.parentGraph(doc,g)?'Override':null, g.name === graphId));
    if(doc.parent&&k!=='macro')for(const g of B.members(doc.parent,'graphs').filter(g=>g.kind===k&&!doc.graphs.some(x=>x.name===g.name))) $(id).append(item('↳ '+g.name,()=>{if(confirm('Override '+g.name+' in this child?')){remember();const next=freshGraph(g.kind,g.name,clone(g.params||[]),g.returns||'void');if(g.pure)next.pure=true; const entry=next.nodes[0], par={id:uid(),kind:'parentCall',x:300,y:60};next.nodes.push(par);if(!g.pure)next.links.push({from:{node:entry.id,pin:'then'},to:{node:par.id,pin:'in'}});const params=g.kind==='event'?B.events[doc.kind][g.name].map(([name,type])=>({name,type})):g.params;for(const p of params)next.links.push({from:{node:entry.id,pin:p.name},to:{node:par.id,pin:p.name}});if(g.kind==='function'&&g.returns!=='void'){const ret={id:uid(),kind:'return',x:560,y:60};next.nodes.push(ret);next.links.push({from:{node:g.pure?entry.id:par.id,pin:'then'},to:{node:ret.id,pin:'in'}},{from:{node:par.id,pin:'result'},to:{node:ret.id,pin:'value'}});}doc.graphs.push(next);selectGraph(g.name);}},'Override…'));
} const v = $('variables'); v.replaceChildren(); for (const x of doc.variables) {
    const r = item(x.name, () => { selected = { variable: x.name }; renderDetails(); }, x.type);
    const dot = el('i', 'dot');
    dot.style.setProperty('--pin', color(x.type));
    r.prepend(dot);
    v.append(r);
} if(doc.parent)for(const x of B.members(doc.parent,'variables'))v.append(item('↳ '+x.name,()=>{selected={inheritedVariable:x.name};renderDetails();},x.type)); if (!B.members(doc,'variables').length)
    v.append(el('div', 'empty', 'No variables yet')); $('localsHeader').hidden=graph()?.kind!=='function';$('locals').replaceChildren();for(const x of graph()?.locals||[])$('locals').append(item(x.name,()=>{selected={local:x.name};renderDetails();},x.type)); $('componentsHeader').hidden = doc.kind !== 'Screen'; $('components').replaceChildren(); if (doc.kind === 'Screen')
    for (const c of doc.components || [])
        $('components').append(item(c.name, () => { selected = { component: c.name }; renderDetails(); }, 'Behaviour'));
    $('hierarchiesHeader').hidden=$('hierarchyImportRow').hidden=doc.kind!=='Screen';$('hierarchies').replaceChildren();for(const h of doc.hierarchies||[])$('hierarchies').append(item(h.name,()=>{selected={hierarchy:h.name,entity:h.entities[0].name};renderDetails();},h.mode+' · '+h.entities.length)); }
function allPalette(q, port) { q = q.trim().toLowerCase(); let items = B.palette(doc, graph()).filter(x => !q || [x.d.title, x.d.category, x.d.description, ...x.d.inputs.map(p => p.type), ...x.d.outputs.map(p => p.type)].filter(Boolean).join(' ').toLowerCase().includes(q)); if (port) {
    items = items.filter(x => (port.dir === 'out' ? x.d.inputs : x.d.outputs).some(p => p.type === port.type));
} return items.sort((a, b) => a.d.category.localeCompare(b.d.category) || a.d.title.localeCompare(b.d.title)); }
function fillPalette(target, q, port, choose) { target.replaceChildren(); const items = allPalette(q, port); let cat = ''; for (const x of items) {
    if (x.d.category !== cat) {
        cat = x.d.category;
        target.append(el('div', 'category', cat));
    }
    const r = item(x.d.title, () => choose(x), x.d.inputs.filter(p => p.type !== 'exec').map(p => p.type).join(', '));
    r.title = x.d.description || x.d.title;
    r.draggable = target === $('palette');
    r.ondragstart = e => e.dataTransfer.setData('application/x-nexgen-node', JSON.stringify(x.n));
    target.append(r);
} if (!items.length)
    target.append(el('div', 'empty', 'No matching nodes')); return items.length; }
function renderPalette() { $('nodeCount').textContent = fillPalette($('palette'), $('search').value, null, x => addNode(x.n)); }
function addNode(n, pos = null, from = null) { remember(); const v = $('viewport').getBoundingClientRect(); const p = pos || worldPoint(v.left + v.width / 2 - 109 * view.z, v.top + v.height / 2 - 50 * view.z); if (!pos) {
    const overlaps = () => graph().nodes.some(n => p.x < (n.x || 0) + 238 && p.x + 238 > (n.x || 0) && p.y < (n.y || 0) + (document.getElementById('node-' + n.id)?.offsetHeight || 140) + 18 && p.y + 145 > (n.y || 0));
    let tries = 0;
    while (overlaps() && tries++ < 30) {
        p.x += 255;
        if (p.x > worldPoint(v.right, v.bottom).x - 230) {
            p.x = worldPoint(v.left, v.top).x + 30;
            p.y += 170;
        }
    }
} const node = { ...n, id: uid(), x: Math.round(p.x), y: Math.round(p.y), defaults: clone(n.defaults || {}) }; graph().nodes.push(node); if (from) {
    const d = B.describe(doc, graph(), node);
    const pin = (from.dir === 'out' ? d.inputs : d.outputs).find(p => p.type === from.type);
    if (pin) {
        const other = { node: node.id, pin: pin.name, dir: from.dir === 'out' ? 'in' : 'out', type: pin.type };
        connect(from, other, false);
    }
} selected = { node: node.id }; armed = null; hideContext(); render(); if (!pos) {
    const r = document.getElementById('node-' + node.id).getBoundingClientRect();
    if (r.right > v.right || r.bottom > v.bottom || r.left < v.left || r.top < v.top)
        fit();
} }
function showContext(x, y, port = null) { contextPos = worldPoint(x, y); filterPort = port; const c = $('context'), v = $('viewport').getBoundingClientRect(); c.hidden = false; c.style.left = Math.max(0, Math.min(x - v.left, v.width - 290)) + 'px'; c.style.top = Math.max(0, Math.min(y - v.top, v.height - 350)) + 'px'; const i = c.querySelector('input'); i.value = ''; renderContext(); i.focus(); }
function renderContext() { const c = $('context'); fillPalette(c.querySelector('div'), c.querySelector('input').value, filterPort, x => addNode(x.n, contextPos, filterPort)); }
function hideContext() { $('context').hidden = true; filterPort = null; }
function worldPoint(x, y) { const r = $('viewport').getBoundingClientRect(); return { x: (x - r.left - view.x) / view.z, y: (y - r.top - view.y) / view.z }; }
function transform() { $('world').style.transform = `translate(${view.x}px,${view.y}px) scale(${view.z})`; $('zoomLabel').textContent = Math.round(view.z * 100) + '%'; $('viewport').style.backgroundSize = 22 * view.z + 'px ' + 22 * view.z + 'px'; $('viewport').style.backgroundPosition = view.x + 'px ' + view.y + 'px'; }
function fit() { const g = graph(), v = $('viewport'); if (!g || !g.nodes.length)
    return; const minX = Math.min(...g.nodes.map(n => n.x || 0)), minY = Math.min(...g.nodes.map(n => n.y || 0)), maxX = Math.max(...g.nodes.map(n => (n.x || 0) + 230)), maxY = Math.max(...g.nodes.map(n => (n.y || 0) + (document.getElementById('node-' + n.id)?.offsetHeight || 130))); view.z = Math.max(.25, Math.min(1.15, (v.clientWidth - 70) / (maxX - minX), (v.clientHeight - 65) / (maxY - minY))); view.x = (v.clientWidth - (maxX - minX) * view.z) / 2 - minX * view.z; view.y = (v.clientHeight - (maxY - minY) * view.z) / 2 - minY * view.z; transform(); }
function zoom(f, x, y) { const v = $('viewport').getBoundingClientRect(); x ??= v.left + v.width / 2; y ??= v.top + v.height / 2; const p = worldPoint(x, y); view.z = Math.max(.25, Math.min(2.4, view.z * f)); view.x = x - v.left - p.x * view.z; view.y = y - v.top - p.y * view.z; transform(); }
function portKey(n, dir, p) { return n + '|' + dir + '|' + p; }
let portElements = new Map();
function renderGraph() { const g = graph(); $('graphName').textContent = g ? doc.name + '  /  ' + g.name : 'No graph'; $('nodes').replaceChildren(); portElements = new Map(); if (!g)
    return; const connected = new Set();
    for (const l of g.links) { connected.add(portKey(l.from.node, 'out', l.from.pin)); connected.add(portKey(l.to.node, 'in', l.to.pin)); }
    const fragment = document.createDocumentFragment();
    for (const n of g.nodes) {
    const d = B.describe(doc, g, n) || {title:'Invalid node: '+n.kind,category:'Invalid',inputs:[],outputs:[]};
    const x = el('div', 'node ' + (n.kind==='comment'?'comment ': '') + (n.kind === 'entry' ? 'entry' : d.category === 'Flow' ? 'flow' : !d.inputs.some(p => p.type === 'exec') ? 'pure' : ''));
    x.id = 'node-' + n.id;
    x.style.left = (n.x || 0) + 'px';
    x.style.top = (n.y || 0) + 'px';
    if (selected?.node === n.id)
        x.classList.add('selected');
    if(n.breakpoint) x.classList.add('breakpoint');
    if (last?.diagnostics.some(a => a.graph === g.name && a.node === n.id && a.severity !== 'warning'))
        x.classList.add('error');
    const h = el('div', 'nodeheader');
    h.append(el('small', '', d.category), el('div', '', d.title));
    x.append(h);
    h.onpointerdown = e => { if (e.button !== 0)
        return; e.stopPropagation(); selected = { node: n.id }; renderDetails(); document.querySelectorAll('.node.selected').forEach(e => e.classList.remove('selected')); x.classList.add('selected'); drag = { type: 'node', n, x, clientX: e.clientX, clientY: e.clientY, ox: n.x || 0, oy: n.y || 0, moved: false }; };
    x.onclick = e => { if (e.target.closest('.port'))
        return; selected = { node: n.id }; renderDetails(); document.querySelectorAll('.node.selected').forEach(e => e.classList.remove('selected')); x.classList.add('selected'); };
    const body = el('div', 'nodebody');
    for (const [dir, pins] of [['in', d.inputs], ['out', d.outputs]]) {
        const col = el('div', 'pins');
        if (!pins.length)
            col.style.display = 'none';
        if (pins.every(p => p.type === 'exec'))
            col.style.flex = '0 0 36px';
        for (const p of pins) {
            const row = el('div', 'portrow ' + dir), port = el('button', 'port ' + (p.type === 'exec' ? 'exec' : ''));
            port.style.setProperty('--pin', color(p.type));
            port.title = p.name + ' : ' + p.type;
            const data = { node: n.id, pin: p.name, dir, type: p.type };
            port.dataset.port = portKey(n.id, dir, p.name);
            portElements.set(port.dataset.port, { el: port, data });
            if (connected.has(port.dataset.port))
                port.classList.add('connected');
            if (armed && portKey(armed.node, armed.dir, armed.pin) === port.dataset.port)
                port.classList.add('chosen');
            const label = el('span', 'pname', n.kind==='switch'&&p.name.startsWith('case')?String(n.cases[Number(p.name.slice(4))]):p.name === 'in' || p.name === 'then' ? '' : p.name);
            if (dir === 'in') {
                row.append(port, label);
                if (p.type !== 'exec' && !port.classList.contains('connected')) {
                    const value = n.defaults?.[p.name] ?? p.default ?? B.defaultValue(p.type);
                    row.append(el('span', 'pvalue', JSON.stringify(value)));
                }
            }
            else
                row.append(label, port);
            port.onpointerdown = e => { if (e.button !== 0)
                return; e.preventDefault(); e.stopPropagation(); drag = { type: 'wire', start: data, clientX: e.clientX, clientY: e.clientY, moved: false }; };
            port.oncontextmenu = e => { e.preventDefault(); e.stopPropagation(); remember(); g.links = g.links.filter(l => !(dir === 'in' ? l.to.node === n.id && l.to.pin === p.name : l.from.node === n.id && l.from.pin === p.name)); armed = null; render(); };
            col.append(row);
        }
        body.append(col);
    }
    x.append(body);
    fragment.append(x);
} $('nodes').append(fragment); transform(); drawWires(); if(typeof debugDecorate==='function')debugDecorate(); }
function pinPos(p, dir) { const e = portElements.get(portKey(p.node, dir, p.pin))?.el; if (!e)
    return null; const r = e.getBoundingClientRect(); return worldPoint(r.left + r.width / 2, r.top + r.height / 2); }
function curve(a, b) { const bend = Math.max(42, Math.abs(b.x - a.x) * .48); return `M ${a.x} ${a.y} C ${a.x + bend} ${a.y}, ${b.x - bend} ${b.y}, ${b.x} ${b.y}`; }
// Keep existing wire elements while dragging. Read each pin position only once per draw.
let wirePaths = new Map(), wirePreview = null;
function drawWires(pointer = null) {
    const s = $('wires'), next = new Map(), positions = new Map();
    const position = (p, dir) => {
        const key = portKey(p.node, dir, p.pin);
        if (!positions.has(key)) positions.set(key, pinPos(p, dir));
        return positions.get(key);
    };
    const attr = (element, key, value) => { if (element.getAttribute(key) !== String(value)) element.setAttribute(key, value); };
    if (wirePreview) { wirePreview.remove(); wirePreview = null; }
    for (const l of graph()?.links || []) {
        if (globalThis.NexgenOrganise?.sameFold(l)) continue;
        const a = position(l.from, 'out'), b = position(l.to, 'in');
        if (!a || !b) continue;
        const t = portElements.get(portKey(l.from.node, 'out', l.from.pin))?.data.type;
        if (!t) continue;
        const d = typeof NexgenPolish !== 'undefined' ? NexgenPolish.routePath(l, a, b, curve) : curve(a, b);
        let p = wirePaths.get(l);
        if (!p || p.parentNode !== s) {
            p = svg('path', { class: 'wire' });
            p.onclick = e => { e.stopPropagation(); selected = { link: l }; renderDetails(); };
            p.oncontextmenu = e => { e.preventDefault(); e.stopPropagation(); remember(); graph().links = graph().links.filter(x => x !== l); render(); };
            s.append(p);
        }
        attr(p, 'd', d); attr(p, 'stroke', color(t));
        next.set(l, p);
    }
    for (const [l, p] of wirePaths) if (!next.has(l)) p.remove();
    wirePaths = next;
    if (pointer && drag?.type === 'wire') {
        const p = drag.start, a = position(p, p.dir), b = worldPoint(pointer.clientX, pointer.clientY);
        if (a) {
            wirePreview = svg('path', { d: p.dir === 'out' ? curve(a, b) : curve(b, a), stroke: color(p.type), class: 'wire', 'stroke-dasharray': '5 4' });
            s.append(wirePreview);
        }
    }
}
// Check the proposed connection before changing the graph or undo history.
function connectionError(message) { $('status').textContent = message; return false; }
function connect(a, b, record = true) {
    if (a.dir === b.dir || a.type !== b.type || a.node === b.node)
        return connectionError('Incompatible pins');
    const from = a.dir === 'out' ? a : b, to = a.dir === 'in' ? a : b, g = graph();
    const byId = new Map(g.nodes.map(n => [n.id, B.describe(doc, g, n)]));
    const output = byId.get(from.node)?.outputs.find(p => p.name === from.pin);
    const input = byId.get(to.node)?.inputs.find(p => p.name === to.pin);
    if (!output || !input || output.type !== input.type || output.type !== a.type)
        return connectionError('Pin no longer exists or its type has changed');
    if (g.links.some(l => l.from.node === from.node && l.from.pin === from.pin && l.to.node === to.node && l.to.pin === to.pin))
        return true;
    const exec = output.type === 'exec';
    const next = g.links.filter(l => !(exec ? l.from.node === from.node && l.from.pin === from.pin : l.to.node === to.node && l.to.pin === to.pin));
    // Ignore the wire being replaced. Execution and data cycles are checked separately,
    // just as in the compiler; loops are explicit nodes rather than back edges.
    const edges = new Map();
    for (const l of next) {
        const p = byId.get(l.from.node)?.outputs.find(p => p.name === l.from.pin);
        if (!p || (p.type === 'exec') !== exec) continue;
        if (!edges.has(l.from.node)) edges.set(l.from.node, []);
        edges.get(l.from.node).push(l.to.node);
    }
    const todo = [to.node], seen = new Set();
    while (todo.length) {
        const id = todo.pop();
        if (id === from.node) return connectionError(exec ? 'Execution cycle — use a loop node instead' : 'Data cycle — use a variable to store the previous value');
        if (seen.has(id)) continue;
        seen.add(id);
        for (const target of edges.get(id) || []) todo.push(target);
    }
    if (record) remember();
    g.links = [...next, {from: {node: from.node, pin: from.pin}, to: {node: to.node, pin: to.pin}}];
    return true;
}
function field(parent, label, type, value, onchange) { parent.append(el('label', '', label)); let inp; if (type === 'bool') {
    inp = el('select');
    for (const v of ['false', 'true']) {
        const o = el('option', '', v);
        o.value = v;
        inp.append(o);
    }
    inp.value = String(value ?? false);
    inp.onchange = () => onchange(inp.value === 'true');
}
else if (B.catalog.enums[type]) {
    inp = el('select');
    for (const v of B.catalog.enums[type]) {
        const o = el('option', '', v);
        o.value = v;
        inp.append(o);
    }
    inp.value = value ?? B.defaultValue(type);
    inp.onchange = () => onchange(inp.value);
}
else {
    inp = el('input');
    inp.value = type === 'string' ? value ?? '' : JSON.stringify(value ?? B.defaultValue(type));
    inp.onchange = () => { try {
        const v = type === 'string' ? inp.value : JSON.parse(inp.value);
        B.literal(type, v);
        onchange(v);
    }
    catch (e) {
        inp.setCustomValidity(e.message);
        inp.reportValidity();
        inp.setCustomValidity('');
    } };
} if (type.endsWith('@') || type === 'Voice' || type === 'Tween')
    inp.disabled = true; parent.append(inp); return inp; }
function action(parent, label, fn, cls = '') { const b = el('button', cls, label); b.onclick = fn; parent.append(b); return b; }
function renderDetails() {
    const d = $('details');
    d.replaceChildren();
    if(selected?.local){const v=(graph()?.locals||[]).find(v=>v.name===selected.local);if(!v)return;d.append(el('strong','',v.name),el('p','',v.type+' · Local to '+graph().name));field(d,'Default value',v.type,v.default,x=>{remember();v.default=x;render();});action(d,'Delete local',()=>{if(!confirm('Delete this local and its nodes?'))return;remember();const g=graph(),ids=new Set(g.nodes.filter(n=>['getLocal','setLocal'].includes(n.kind)&&n.ref===v.name).map(n=>n.id));g.locals=g.locals.filter(x=>x!==v);g.nodes=g.nodes.filter(n=>!ids.has(n.id));g.links=g.links.filter(l=>!ids.has(l.from.node)&&!ids.has(l.to.node));selected=null;render();},'danger');return;}
    if(selected?.parent){const p=doc.parent; if(!p)return;d.append(el('strong','',p.name),el('p','','Inherited class. Its variables and callable functions are available; choose an inherited graph to override it. Parent code is compiled as an abstract AngelScript class.'));action(d,'Save parent graph',()=>download(p.name+'.bp.json',JSON.stringify(p,null,2),'application/json'));action(d,'Replace parent',()=>$('parentFile').click());action(d,'Remove parent',()=>{if(confirm('Remove parent? Inherited nodes may need repair.')){remember();delete doc.parent;selected=null;render();compile();}},'danger');return;}
    if(selected?.inheritedVariable){const v=B.members(doc.parent,'variables').find(v=>v.name===selected.inheritedVariable);d.append(el('strong','',v.name),el('p','',v.type+' · Inherited variable'),el('p','','Default: '+JSON.stringify(v.default)));return;}
    if(selected?.hierarchy){hierarchyDetails(d);return;}
    if(selected?.interface){
        let i;try{i=B.interfaceRegistry(doc).get(selected.interface);}catch(e){d.append(el('p','',e.message));return;}if(!i)return;
        const local=(doc.interfaces||[]).find(x=>x.name===i.name);
        d.append(el('strong','',i.name),el('p','','Typed contract for messaging any entity that implements it. The first matching behaviour receives the call; Missing runs if none exists.'));
        for(const m of i.methods)d.append(el('p','',m.returns+' '+m.name+'('+m.params.map(p=>p.type+' '+p.name).join(', ')+')'));
        action(d,'Export interface',()=>download(i.name+'.bp.interface.json',JSON.stringify({version:1,kind:'BlueprintInterface',name:i.name,methods:i.methods},null,2),'application/json'));
        if(!local?.implement)action(d,'Implement in this Blueprint',()=>installInterface(i,true));
        else action(d,'Stop implementing',()=>{remember();local.implement=false;render();});
        if(local)action(d,'Remove local interface',()=>{if(!confirm('Remove this local declaration? Message nodes may need repair.'))return;remember();doc.interfaces=doc.interfaces.filter(x=>x!==local);selected=null;render();},'danger');return;
    }
    if(selected?.dispatcher){const x=(doc.dispatchers||[]).find(x=>x.name===selected.dispatcher);if(!x)return;d.append(el('strong','',x.name),el('p','','Typed multicast event on this instance. Bind matching void functions, broadcast arguments, or unbind handlers. Parameters: '+(x.params.map(p=>p.name+':'+p.type).join(', ')||'none')));action(d,'Create handler',()=>{const name='On'+x.name;validName(name);remember();doc.graphs.push(freshGraph('function',name,clone(x.params),'void'));selectGraph(name);});action(d,'Delete dispatcher',()=>{if(!confirm('Delete dispatcher and its nodes?'))return;remember();doc.dispatchers=doc.dispatchers.filter(z=>z!==x);for(const g of doc.graphs){const ids=new Set(g.nodes.filter(n=>['bind','unbind','unbindAll','broadcast'].includes(n.kind)&&n.ref===x.name).map(n=>n.id));g.nodes=g.nodes.filter(n=>!ids.has(n.id));g.links=g.links.filter(l=>!ids.has(l.from.node)&&!ids.has(l.to.node));}selected=null;render();},'danger');return;}
    if (selected?.component) {
        const c = (doc.components || []).find(c => c.name === selected.component);
        if (!c) {
            selected = null;
            return renderDetails();
        }
        d.append(el('strong', '', c.name), el('p', '', 'Reusable Behaviour asset. Search for Attach ' + c.name + ' in the node library. Its graph source is embedded in this screen Blueprint.'));
        action(d, 'Save component graph', () => download(c.name + '.bp.json', JSON.stringify(c, null, 2), 'application/json'));
        action(d, 'Remove component', () => { if (!confirm('Remove this component and its Attach nodes?'))
            return; if((doc.hierarchies||[]).some(h=>h.entities.some(e=>e.behaviours.includes(c.name))))return alert('Remove this Behaviour from its entity hierarchies first.'); remember(); doc.components = doc.components.filter(x => x !== c); if(doc.sharedComponents){doc.sharedComponents=doc.sharedComponents.filter(name=>name!==c.name);if(!doc.sharedComponents.length)delete doc.sharedComponents;} for (const g of doc.graphs) {
            const ids = new Set(g.nodes.filter(n => n.kind === 'attach' && n.ref === c.name).map(n => n.id));
            g.nodes = g.nodes.filter(n => !ids.has(n.id));
            g.links = g.links.filter(l => !ids.has(l.from.node) && !ids.has(l.to.node));
        } selected = null; render(); }, 'danger');
        return;
    }
    if (selected?.variable) {
        const v = doc.variables.find(v => v.name === selected.variable);
        if (!v) {
            selected = null;
            return renderDetails();
        }
        d.append(el('strong', '', v.name), el('p', '', v.type + ' · Instance variable'));
        field(d, 'Default value', v.type, v.default, x => { remember(); v.default = x; render(); });
        action(d, 'Delete variable', () => { if (!confirm('Delete variable and its Get/Set nodes?'))
            return; remember(); doc.variables = doc.variables.filter(x => x !== v); for (const g of doc.graphs) {
            const ids = new Set(g.nodes.filter(n => ['get', 'set'].includes(n.kind) && n.ref === v.name).map(n => n.id));
            g.nodes = g.nodes.filter(n => !ids.has(n.id));
            g.links = g.links.filter(l => !ids.has(l.from.node) && !ids.has(l.to.node));
        } selected = null; render(); }, 'danger');
        return;
    }
    if (selected?.link) {
        const l = selected.link;
        d.append(el('strong', '', 'Connection'), el('p', '', l.from.node + '.' + l.from.pin + ' → ' + l.to.node + '.' + l.to.pin));
        action(d, 'Disconnect', () => { remember(); graph().links = graph().links.filter(x => x !== l); selected = null; render(); }, 'danger');
        return;
    }
    const n = graph()?.nodes.find(n => n.id === selected?.node);
    if (!n) {
        d.append(el('p', '', 'Select a node or variable to edit its properties. Unconnected data pins use their default values.'));
        return;
    }
    const spec = B.describe(doc, graph(), n);
    if (!spec){d.append(el('p','','This node is not valid in the current graph. Remove it or restore its missing definition.'));if(n.kind!=='entry')action(d,'Delete node',deleteSelected,'danger');return;}
    d.append(el('strong', '', spec.title), el('p', '', spec.description || spec.category));
    for (const p of spec.inputs.filter(p => p.type !== 'exec')) {
        const l = graph().links.find(l => l.to.node === n.id && l.to.pin === p.name);
        if (l) {
            d.append(el('label', '', p.name + ' · ' + p.type), el('p', '', 'Connected'));
            action(d, 'Disconnect ' + p.name, () => { remember(); graph().links = graph().links.filter(x => x !== l); render(); });
        }
        else
            field(d, p.name + ' · ' + p.type, p.type, n.defaults?.[p.name] ?? p.default ?? B.defaultValue(p.type), v => { remember(); (n.defaults ??= {})[p.name] = v; render(); });
    }
    if(n.kind==='switch'){d.append(el('h3','','SWITCH CASES'));n.cases.forEach((v,i)=>field(d,'case'+i,n.type,v,x=>{if(n.cases.some((v,j)=>j!==i&&v===x))return alert('Duplicate case');remember();n.cases[i]=x;render();}));action(d,'Add case',()=>{if(n.cases.length>=16)return alert('Maximum 16 cases');const v=n.type==='string'?'Case'+n.cases.length:Math.max(...n.cases)+1;B.literal(n.type,v);remember();n.cases.push(v);render();});if(n.cases.length>1)action(d,'Remove last case',()=>{remember();const pin='case'+(n.cases.length-1);n.cases.pop();graph().links=graph().links.filter(l=>l.from.node!==n.id||l.from.pin!==pin);render();});}
    if(n.kind==='timeline'){timelineDetails(d,n);trackDetails(d,n);}
    if(n.kind==='stateMachine')machineDetails(d,n);
    if(n.kind==='comment'){field(d,'Comment','string',n.text||'',value=>{remember();n.text=value;render();});}
    if (n.kind !== 'entry' && n.kind!=='macroExit') {
        if(spec.inputs.some(p=>p.type==='exec'))action(d,n.breakpoint?'Remove breakpoint':'Set breakpoint',()=>{n.breakpoint=!n.breakpoint;dirty=true;render();if(typeof debugSync==='function')debugSync().catch(e=>debugMessage(e.message));});
        action(d, 'Duplicate', () => addNode({ ...clone(n), id: undefined }, { x: n.x + 25, y: n.y + 35 }));
        action(d, 'Delete node', deleteSelected, 'danger');
    }
}
function timelineDetails(d,n,title='TIMELINE CURVE',allowReset=true){d.append(el('h3','',title));if(!n.keys){d.append(el('p','','Default curve: linear or smoothstep from 0 to 1. Add keys to customize the normalized track.'));action(d,'Edit keyframes',()=>{remember();n.keys=[{time:0,value:0,mode:'linear'},{time:1,value:1,mode:'linear'}];render();});return;}
    const keys=n.keys,min=Math.min(0,...keys.map(k=>k.value)),max=Math.max(1,...keys.map(k=>k.value)),coord=k=>[10+k.time*210,90-(k.value-min)/(max-min)*80];const chart=svg('svg',{viewBox:'0 0 230 100',width:'100%',height:'100'});chart.append(svg('rect',{x:10,y:10,width:210,height:80,fill:'#111820',stroke:'#394b5d'}));let pts=[];for(let i=0;i<keys.length-1;i++){const a=keys[i],b=keys[i+1];for(let j=0;j<=12;j++){let t=j/12,u=a.mode==='step'&&j<12?0:a.mode==='smooth'?t*t*(3-2*t):t;pts.push(coord({time:a.time+(b.time-a.time)*t,value:a.value+(b.value-a.value)*u}).join(','));}}chart.append(svg('polyline',{points:pts.join(' '),stroke:'#80b9ff','stroke-width':2,fill:'none'}));for(const k of keys){const [x,y]=coord(k);chart.append(svg('circle',{cx:x,cy:y,r:3,fill:'#f6c95c'}));}d.append(chart);
    const commit=next=>{try{B.validateKeys(next);remember();n.keys=next;render();}catch(e){alert(e.message);renderDetails();}};
    keys.forEach((k,i)=>{const r=el('div','curveRow');const time=el('input');time.type='number';time.step='0.01';time.value=k.time;time.title='Normalized time';time.disabled=i===0||i===keys.length-1;const val=el('input');val.type='number';val.step='0.1';val.value=k.value;val.title='Curve value';const mode=el('select');for(const m of ['linear','smooth','step']){const opt=el('option','',m);opt.value=m;mode.append(opt);}mode.value=k.mode||'linear';mode.disabled=i===keys.length-1;const change=()=>{const next=clone(keys);next[i]={time:Number(time.value),value:Number(val.value),mode:mode.value};commit(next);};time.onchange=val.onchange=mode.onchange=change;r.append(time,val,mode);if(i>0&&i<keys.length-1)action(r,'×',()=>commit(keys.filter((_,j)=>j!==i)),'danger');d.append(r);});action(d,'Add key',()=>{if(keys.length>=32)return alert('Maximum 32 keys');let i=0;for(let j=1;j<keys.length-1;j++)if(keys[j+1].time-keys[j].time>keys[i+1].time-keys[i].time)i=j;const a=keys[i],b=keys[i+1],next=clone(keys);next.splice(i+1,0,{time:(a.time+b.time)/2,value:(a.value+b.value)/2,mode:'linear'});commit(next);});if(allowReset)action(d,'Reset to default',()=>{remember();delete n.keys;render();});}
let chosenTrack='',chosenChannel=0;
function trackDetails(d,n){
    d.append(el('h3','','TIMELINE PLAYBACK'));
    const playback=el('select');for(const x of ['forward','reverse','loop','pingpong']){const o=el('option','',x);o.value=x;playback.append(o);}playback.value=n.playback||'forward';playback.onchange=()=>{remember();n.playback=playback.value;render();};d.append(playback);
    d.append(el('h3','','ADDITIONAL TRACKS'));
    action(d,'Add track',()=>modal('Add timeline track',[{name:'name',label:'Output pin name',value:'Track'+((n.tracks||[]).length+1)},{name:'type',label:'Track type',options:['float','Vec2','Vec3','Color']}],v=>{const count={float:1,Vec2:2,Vec3:3,Color:4}[v.type],track={...v,channels:Array.from({length:count},()=>[{time:0,value:0,mode:'linear'},{time:1,value:1,mode:'linear'}])};B.validateTimeline({...n,tracks:[...(n.tracks||[]),track]});remember();(n.tracks??=[]).push(track);chosenTrack=track.name;chosenChannel=0;render();}));
    if(!n.tracks?.length)return;
    const t=n.tracks.find(t=>t.name===chosenTrack)||n.tracks[0];chosenTrack=t.name;
    const select=el('select');for(const x of n.tracks){const o=el('option','',x.name+' ('+x.type+')');o.value=x.name;select.append(o);}select.value=t.name;select.onchange=()=>{chosenTrack=select.value;chosenChannel=0;renderDetails();};d.append(select);
    const channels=t.type==='Color'?['R','G','B','A']:t.type==='Vec3'?['X','Y','Z']:t.type==='Vec2'?['X','Y']:['Value'];chosenChannel=Math.min(chosenChannel,channels.length-1);
    if(channels.length>1){const ch=el('select');channels.forEach((x,i)=>{const o=el('option','',x);o.value=i;ch.append(o);});ch.value=chosenChannel;ch.onchange=()=>{chosenChannel=Number(ch.value);renderDetails();};d.append(ch);}
    const channel=chosenChannel;timelineDetails(d,{get keys(){return t.channels[channel];},set keys(x){t.channels[channel]=x;}},t.name+' / '+channels[channel],false);
    action(d,'Remove track',()=>{if(!confirm('Remove track and its outgoing wires?'))return;remember();n.tracks=n.tracks.filter(x=>x!==t);graph().links=graph().links.filter(l=>l.from.node!==n.id||l.from.pin!==t.name);render();},'danger');
}
function machineDetails(d,n){
    d.append(el('h3','','STATE MACHINE'),el('p','','Wire Update dt and Boolean transition conditions. The first eligible transition wins; state entry and elapsed time are available on output pins.'));
    const states=n.states, transitions=n.transitions;
    const position=i=>({x:58+(i%2)*116,y:24+Math.floor(i/2)*66});
    const h=Math.max(70,Math.ceil(states.length/2)*66),chart=svg('svg',{viewBox:'0 0 232 '+h,width:'100%',height:h});
    chart.append(svg('rect',{x:0,y:0,width:232,height:h,rx:4,fill:'#111820'}));
    const defs=svg('defs',{}),marker=svg('marker',{id:'stateArrow',markerWidth:7,markerHeight:7,refX:6,refY:3.5,orient:'auto'});marker.append(svg('path',{d:'M0 0 L7 3.5 L0 7 Z',fill:'#80b9ff'}));defs.append(marker);chart.append(defs);
    for(const t of transitions){if(t.from==='*')continue;const a=position(states.findIndex(x=>x.name===t.from)),b=position(states.findIndex(x=>x.name===t.to));const dx=b.x-a.x,dy=b.y-a.y,len=Math.hypot(dx,dy),ux=dx/len,uy=dy/len;const sx=a.x+ux*45,sy=a.y+uy*13,ex=b.x-ux*47,ey=b.y-uy*14;const path=svg('path',{d:`M ${sx} ${sy} Q ${(sx+ex)/2-uy*15} ${(sy+ey)/2+ux*15} ${ex} ${ey}`,stroke:'#80b9ff','stroke-width':1.4,fill:'none','marker-end':'url(#stateArrow)'});chart.append(path);}
    states.forEach((state,i)=>{const {x,y}=position(i),g=svg('g',{});g.append(svg('rect',{x:x-47,y:y-13,width:94,height:26,rx:5,fill:state.name===n.initial?'#285a53':'#253549',stroke:state.name===n.initial?'#65d8a2':'#58718c'}));const text=svg('text',{x,y:y+4,'text-anchor':'middle','font-size':10,fill:'#e6edf8'});text.textContent=state.name.length>15?state.name.slice(0,13)+'…':state.name;g.append(text);g.style.cursor='pointer';g.onclick=()=>editState(state);chart.append(g);});d.append(chart);
    d.append(el('label','','Initial state'));const initial=el('select');for(const st of states){const o=el('option','',st.name);o.value=st.name;initial.append(o);}initial.value=n.initial;initial.onchange=()=>{remember();n.initial=initial.value;render();};d.append(initial);
    function editState(old){const state=old||{name:'State'+(states.length+1),clip:'idle',first:0,last:0,fps:8,loop:true,blend:.25,speed:1};const fields=[{name:'name',label:'State name',value:state.name}];if(n.mode==='sprite')fields.push({name:'first',label:'First frame',value:state.first},{name:'last',label:'Last frame',value:state.last},{name:'fps',label:'Frames per second',value:state.fps});if(n.mode==='model')fields.push({name:'clip',label:'Animation clip name',value:state.clip},{name:'blend',label:'Blend seconds',value:state.blend},{name:'speed',label:'Playback speed',value:state.speed});if(n.mode!=='logic')fields.push({name:'loop',label:'Loop clip',options:['true','false'],value:String(state.loop)});
        modal(old?'Edit state':'Add state',fields,v=>{const next={...state,...v};for(const k of ['first','last','fps','blend','speed'])if(Object.hasOwn(v,k))next[k]=Number(v[k]);if(Object.hasOwn(v,'loop'))next.loop=v.loop==='true';const nextStates=old?states.map(x=>x===old?next:x):[...states,next];const nextTransitions=old?transitions.map(t=>({...t,from:t.from===old.name?next.name:t.from,to:t.to===old.name?next.name:t.to})):transitions;const nextInitial=old&&n.initial===old.name?next.name:n.initial;B.validateMachine({...n,states:nextStates,transitions:nextTransitions,initial:nextInitial});remember();n.states=nextStates;n.transitions=nextTransitions;n.initial=nextInitial;if(old&&old.name!==next.name)for(const l of graph().links)if(l.from.node===n.id&&l.from.pin==='state_'+old.name)l.from.pin='state_'+next.name;render();});}
    d.append(el('h3','','STATES'));for(const st of states){const r=el('div','row');r.append(el('span','',st.name));action(r,'Edit',()=>editState(st));if(states.length>1)action(r,'×',()=>{if(!confirm('Remove state, its transitions and wires?'))return;remember();const removed=new Set(transitions.filter(t=>t.from===st.name||t.to===st.name).map(t=>'when_'+t.name));n.states=states.filter(x=>x!==st);n.transitions=transitions.filter(t=>!removed.has('when_'+t.name));if(n.initial===st.name)n.initial=n.states[0].name;if(n.defaults)for(const pin of removed)delete n.defaults[pin];graph().links=graph().links.filter(l=>!(l.from.node===n.id&&l.from.pin==='state_'+st.name)&&!(l.to.node===n.id&&removed.has(l.to.pin)));render();},'danger');d.append(r);}action(d,'Add state',()=>editState(null));
    function editTransition(old){const t=old||{name:'Transition'+(transitions.length+1),from:states[0].name,to:states.at(-1).name};modal(old?'Edit transition':'Add transition',[{name:'name',label:'Condition pin name',value:t.name},{name:'from',label:'From (* means any state)',options:['*',...states.map(x=>x.name)],value:t.from},{name:'to',label:'To state',options:states.map(x=>x.name),value:t.to}],v=>{const next=old?transitions.map(x=>x===old?v:x):[...transitions,v];B.validateMachine({...n,transitions:next});remember();n.transitions=next;if(old&&old.name!==v.name){for(const l of graph().links)if(l.to.node===n.id&&l.to.pin==='when_'+old.name)l.to.pin='when_'+v.name;if(n.defaults&&Object.hasOwn(n.defaults,'when_'+old.name)){n.defaults['when_'+v.name]=n.defaults['when_'+old.name];delete n.defaults['when_'+old.name];}}render();});}
    d.append(el('h3','','TRANSITIONS / PRIORITY'));transitions.forEach((t,i)=>{d.append(el('p','',(i+1)+'. '+t.name+' · '+t.from+' → '+t.to));const r=el('div','row');action(r,'Edit',()=>editTransition(t));if(i>0)action(r,'↑',()=>{remember();[transitions[i-1],transitions[i]]=[transitions[i],transitions[i-1]];render();});action(r,'Remove',()=>{remember();n.transitions=transitions.filter(x=>x!==t);graph().links=graph().links.filter(l=>l.to.node!==n.id||l.to.pin!=='when_'+t.name);if(n.defaults)delete n.defaults['when_'+t.name];render();},'danger');d.append(r);});action(d,'Add transition',()=>editTransition(null));
}
function hierarchyDefaultEntity(name,mode,parent=null){return {name,parent,position:mode==='3D'?[0,0,0]:[0,0],rotation:mode==='3D'?[0,0,0]:0,scale:mode==='3D'?[1,1,1]:[1,1],behaviours:[],visual:{kind:'none'}};}
function hierarchyDetails(d){
    const h=(doc.hierarchies||[]).find(x=>x.name===selected.hierarchy);if(!h)return;
    const order=B.hierarchyOrder(doc,h),root=order[0],current=order.find(e=>e.name===selected.entity)||root,three=h.mode==='3D';selected.entity=current.name;
    d.append(el('strong','',h.name),el('p','',h.mode+' reusable entity tree. Add its Spawn node to any execution graph; connect the root or child reference pins. Transforms are local to the parent.'));
    const coords=new Map();for(const e of order){const parent=coords.get(e.parent)||{x:0,y:0,angle:0,sx:1,sy:1};const angle=(three?e.rotation[1]:e.rotation)*Math.PI/180,px=e.position[0]*parent.sx,py=e.position[three?2:1]*parent.sy,c=Math.cos(parent.angle),sn=Math.sin(parent.angle);coords.set(e.name,{x:parent.x+px*c-py*sn,y:parent.y+px*sn+py*c,angle:parent.angle+angle,sx:parent.sx*e.scale[0],sy:parent.sy*e.scale[three?2:1]});}
    const all=[...coords.values()],minX=Math.min(...all.map(p=>p.x)),maxX=Math.max(...all.map(p=>p.x)),minY=Math.min(...all.map(p=>p.y)),maxY=Math.max(...all.map(p=>p.y)),span=Math.max(maxX-minX,maxY-minY,three?2:64),unit=145/span,at=p=>[116+(p.x-(minX+maxX)/2)*unit,95+(p.y-(minY+maxY)/2)*unit];
    const chart=svg('svg',{viewBox:'0 0 232 190',width:'100%',height:190});chart.append(svg('rect',{x:0,y:0,width:232,height:190,rx:4,fill:'#111820'}));
    for(const e of order)if(e.parent){const a=at(coords.get(e.parent)),b=at(coords.get(e.name));chart.append(svg('line',{x1:a[0],y1:a[1],x2:b[0],y2:b[1],stroke:'#6c829c','stroke-dasharray':'4 3'}));}
    for(const e of order){const [x,y]=at(coords.get(e.name));const g=svg('g',{}),color=e.name===current.name?'#f6c95c':'#80b9ff';g.append(svg('circle',{cx:x,cy:y,r:6,fill:color,stroke:'#18202c','stroke-width':2}));const t=svg('text',{x:x+8,y:y-9,'font-size':9,fill:'#d9e9fb'});t.textContent=e.name.slice(0,15);g.append(t);g.style.cursor='pointer';g.onclick=()=>{selected.entity=e.name;renderDetails();};chart.append(g);}d.append(chart,el('p','',three?'Top-down transform schematic (not a model renderer).':'2D transform schematic (not a texture renderer).'));
    const depth=e=>e.parent?1+depth(order.find(x=>x.name===e.parent)):0;
    d.append(el('h3','','ENTITY TREE'));for(const e of order){const r=item((e.parent?'↳ ':'◆ ')+e.name,()=>{selected.entity=e.name;renderDetails();},e.behaviours.length?e.behaviours.length+' script(s)':(e.visual?.kind||'none'),e===current);r.style.paddingLeft=(8+Math.min(6,depth(e))*12)+'px';d.append(r);}
    const descendants=new Set([current.name]);for(const e of order)if(e.parent&&descendants.has(e.parent))descendants.add(e.name);
    function editTransform(old){const e=old||hierarchyDefaultEntity('Entity'+(order.length+1),h.mode,current.name),parents=old===root?['(root)']:order.filter(x=>!old||!descendants.has(x.name)).map(x=>x.name);
        modal(old?'Edit local transforms':'Add child entity',[{name:'name',label:'Entity name (max 23 characters)',value:e.name},{name:'parent',label:'Parent',options:parents,value:e.parent||'(root)'},{name:'position',label:three?'Position [x,y,z]':'Position [x,y]',value:JSON.stringify(e.position)},{name:'rotation',label:three?'Angles [x,y,z]':'Rotation (degrees)',value:JSON.stringify(e.rotation)},{name:'scale',label:three?'Size [x,y,z]':'Scale [x,y]',value:JSON.stringify(e.scale)}],v=>{const next={...e,name:v.name,parent:v.parent==='(root)'?null:v.parent,position:JSON.parse(v.position),rotation:JSON.parse(v.rotation),scale:JSON.parse(v.scale)};const entities=old?h.entities.map(x=>x===old?next:{...x,parent:x.parent===old.name?next.name:x.parent}):[...h.entities,next];B.hierarchyOrder(doc,{...h,entities});remember();h.entities=entities;if(old&&old.name!==next.name)for(const g of doc.graphs){const ids=new Set(g.nodes.filter(n=>n.kind==='hierarchySpawn'&&n.ref===h.name).map(n=>n.id));for(const l of g.links)if(ids.has(l.from.node)&&l.from.pin==='entity_'+old.name)l.from.pin='entity_'+next.name;}selected.entity=next.name;render();});}
    action(d,'Add child',()=>editTransform(null));
    d.append(el('h3','',current.name.toUpperCase()),el('p','','Local position '+JSON.stringify(current.position)+' · rotation '+JSON.stringify(current.rotation)+' · scale '+JSON.stringify(current.scale)));
    action(d,'Edit transforms',()=>editTransform(current));
    action(d,'Edit visual',()=>{const v=current.visual||{kind:'none'};modal('Entity visual',[{name:'kind',label:'Visual type',options:three?['none','shape','model']:['none','rect','disc','image'],value:v.kind},...(three?[{name:'shape',label:'Primitive shape',options:B.catalog.enums.Solid,value:v.shape||'SolidBox'}]:[{name:'size',label:'Rectangle / image dimensions [w,h]',value:JSON.stringify(v.size||[64,64])},{name:'radius',label:'Disc radius',value:v.radius??32}]),{name:'color',label:'Primitive RGBA color [0–1]',value:JSON.stringify(v.color||[1,1,1,1])},{name:'asset',label:'Image/model asset path, e.g. asset:player.glb',value:v.asset||''}],v=>{const next={kind:v.kind};if(['rect','disc','shape'].includes(v.kind))next.color=JSON.parse(v.color);if(v.kind==='shape')next.shape=v.shape;if(['rect','image'].includes(v.kind))next.size=JSON.parse(v.size);if(v.kind==='disc')next.radius=Number(v.radius);if(['image','model'].includes(v.kind))next.asset=v.asset;B.hierarchyOrder(doc,{...h,entities:h.entities.map(e=>e===current?{...e,visual:next}:e)});remember();current.visual=next;render();});});
    d.append(el('p','','Visual: '+(current.visual?.kind||'none')));
    d.append(el('h3','','BEHAVIOURS'));for(const c of current.behaviours){const r=el('div','row');r.append(el('span','',c));action(r,'Remove',()=>{remember();current.behaviours=current.behaviours.filter(x=>x!==c);render();},'danger');d.append(r);}
    const available=(doc.components||[]).filter(c=>!current.behaviours.includes(c.name));action(d,'Attach Behaviour',()=>{if(!available.length)return alert('Import a Behaviour component first.');modal('Attach Behaviour',[{name:'name',label:'Behaviour class',options:available.map(c=>c.name)}],v=>{if(current.behaviours.length>=8)throw Error('Maximum 8 behaviours per entity');remember();current.behaviours.push(v.name);render();});});
    if(current!==root)action(d,'Delete subtree',()=>{if(!confirm('Remove this entity and all descendants? Their output wires will be removed.'))return;remember();h.entities=h.entities.filter(e=>!descendants.has(e.name));for(const g of doc.graphs){const ids=new Set(g.nodes.filter(n=>n.kind==='hierarchySpawn'&&n.ref===h.name).map(n=>n.id));g.links=g.links.filter(l=>!ids.has(l.from.node)||!l.from.pin.startsWith('entity_')||!descendants.has(l.from.pin.slice(7)));}selected.entity=root.name;render();},'danger');
    d.append(el('h3','','TEMPLATE'));action(d,'Export hierarchy',()=>download(h.name+'.bp.hierarchy.json',JSON.stringify({version:1,kind:'BlueprintHierarchy',hierarchy:h},null,2),'application/json'));
    action(d,'Remove hierarchy',()=>{if(!confirm('Remove template and its Spawn nodes?'))return;remember();doc.hierarchies=doc.hierarchies.filter(x=>x!==h);for(const g of doc.graphs){const ids=new Set(g.nodes.filter(n=>n.kind==='hierarchySpawn'&&n.ref===h.name).map(n=>n.id));g.nodes=g.nodes.filter(n=>!ids.has(n.id));g.links=g.links.filter(l=>!ids.has(l.from.node)&&!ids.has(l.to.node));}selected=null;render();},'danger');
}
function deleteSelected() { const g = graph(), n = g?.nodes.find(n => n.id === selected?.node); if (n && n.kind !== 'entry' && n.kind!=='macroExit') {
    remember();
    g.nodes = g.nodes.filter(x => x !== n);
    g.links = g.links.filter(l => l.from.node !== n.id && l.to.node !== n.id);
    selected = null;
    render();
}
else if (selected?.link) {
    remember();
    g.links = g.links.filter(l => l !== selected.link);
    selected = null;
    render();
} }
function compile() { last = B.compile(doc); $('diagnostics').replaceChildren(); if (last.ok)
    $('diagnostics').append(el('div', 'diag ok', '✓ Graph compiled to AngelScript. Run in Nexgen to validate engine bindings and gameplay.')); for (const a of last.diagnostics) {
    const r = el('button', 'diag ' + (a.severity === 'warning' ? 'warn' : ''), (a.severity === 'warning' ? 'Warning: ' : 'Error: ') + (a.graph ? a.graph + ' / ' : '') + a.message);
    r.onclick = () => { if (doc.graphs.some(g => g.name === a.graph)) {
        selectGraph(a.graph);
        selected = { node: a.node };
        renderGraph();
        renderDetails();
    } };
    $('diagnostics').append(r);
} $('source').textContent = last.source; renderGraph(); updateStatus(); return last; }
function download(name, data, mime = 'text/plain') { const a = el('a'); const url = URL.createObjectURL(new Blob([data], { type: mime })); a.href = url; a.download = name; a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000); }
function modal(title, fields, apply) { const dlg = $('dialog'), body = $('dialogBody'); $('dialogTitle').textContent = title; body.replaceChildren(); const inputs = {}; for (const f of fields) {
    body.append(el('label', '', f.label));
    let inp;
    if (f.options) {
        inp = el('select');
        for (const v of f.options) {
            const o = el('option', '', v);
            o.value = v;
            inp.append(o);
        }
    }
    else
        inp = el('input');
    inp.value = f.value ?? f.options?.[0] ?? '';
    inputs[f.name] = inp;
    body.append(inp);
} dlg.onclose = () => { if (dlg.returnValue === 'ok') {
    try {
        apply(Object.fromEntries(Object.entries(inputs).map(([k, v]) => [k, v.value])));
    }
    catch (e) {
        alert(e.message);
    }
} }; dlg.returnValue = 'cancel'; dlg.showModal(); }
function validName(name) { if (!B.ident(name) || B.members(doc,'variables').some(v => v.name === name) || B.members(doc,'graphs').some(g => g.name === name) || (doc.dispatchers||[]).some(d=>d.name===name) || Object.hasOwn(B.events[doc.kind], name) || name === doc.name)
    throw Error('Choose a unique valid identifier'); }
function installInterface(i,implement){
    const candidate=clone(doc);candidate.interfaces=(candidate.interfaces||[]).filter(x=>x.name!==i.name);candidate.interfaces.push({name:i.name,methods:clone(i.methods),implement});B.interfaceRegistry(candidate);
    const additions=[];
    if(implement)for(const m of i.methods){const old=B.members(doc,'graphs').find(g=>g.name===m.name);if(old){if(old.kind!=='function'||old.returns!==m.returns||old.params.length!==m.params.length||old.params.some((p,j)=>p.type!==m.params[j].type))throw Error('Existing graph conflicts with '+m.name);continue;}validName(m.name);const g=freshGraph('function',m.name,clone(m.params),m.returns);if(m.returns!=='void'){const ret={id:uid(),kind:'return',x:320,y:60};g.nodes.push(ret);g.links.push({from:{node:g.nodes[0].id,pin:'then'},to:{node:ret.id,pin:'in'}});}additions.push(g);}
    remember();doc.interfaces=candidate.interfaces;doc.graphs.push(...additions);selected={interface:i.name};render();
}
$('addHierarchy').onclick=()=>modal('Create entity hierarchy',[{name:'name',label:'Template name',value:'PlayerRig'},{name:'mode',label:'Scene type',options:['2D','3D']}],v=>{const h={...v,entities:[hierarchyDefaultEntity('Root',v.mode)]},candidate={...doc,hierarchies:[...(doc.hierarchies||[]),h]};B.validateHierarchies(candidate);remember();doc.hierarchies=candidate.hierarchies;selected={hierarchy:h.name,entity:'Root'};render();});
$('importHierarchy').onclick=()=>$('hierarchyFile').click();$('hierarchyFile').onchange=async()=>{const f=$('hierarchyFile').files[0];$('hierarchyFile').value='';if(!f)return;try{if(f.size>512*1024)throw Error('Hierarchy file too large');const v=JSON.parse(await f.text());if(v.version!==1||v.kind!=='BlueprintHierarchy')throw Error('Not a hierarchy file');const h=v.hierarchy,candidate={...doc,hierarchies:[...(doc.hierarchies||[]),h]};B.validateHierarchies(candidate);remember();doc.hierarchies=candidate.hierarchies;selected={hierarchy:h.name,entity:h.entities[0].name};render();}catch(e){alert(e.message);}};
$('addInterface').onclick=()=>modal('Create Blueprint interface',[{name:'name',label:'Interface name',value:'Damageable'},{name:'methods',label:'Methods: Name(param:type):return; Next():void',value:'ApplyDamage(amount:int):int'},{name:'mode',label:'Usage',options:['Reference only','Implement here']}],v=>{if(B.interfaceRegistry(doc).has(v.name))throw Error('Interface already exists');const methods=v.methods.split(';').map(x=>{const m=x.trim().match(/^([A-Za-z_]\w*)\((.*?)\):(.+)$/);if(!m)throw Error('Use Name(param:type):return');return {name:m[1],params:parseParams(m[2]),returns:m[3].trim()};});installInterface({name:v.name,methods},v.mode==='Implement here');});
$('importInterface').onclick=()=>$('interfaceFile').click();
$('interfaceFile').onchange=async()=>{const f=$('interfaceFile').files[0];$('interfaceFile').value='';if(!f)return;try{if(f.size>256*1024)throw Error('Interface file too large');const i=JSON.parse(await f.text());if(i.version!==1||i.kind!=='BlueprintInterface')throw Error('Not a Blueprint interface file');const previous=(doc.interfaces||[]).find(x=>x.name===i.name);if(previous){const canon=x=>JSON.stringify(x.methods.map(m=>({name:m.name,returns:m.returns,params:m.params.map(p=>({name:p.name,type:p.type}))})).sort((a,b)=>a.name.localeCompare(b.name)));if(canon(previous)!==canon(i))throw Error('Conflicting existing interface contract');}installInterface(i,!!previous?.implement);}catch(e){alert(e.message);}};
$('examplesButton').onclick=()=>modal('Open example',[{name:'name',label:'Example graph',options:Object.keys(NexgenExamples)}],v=>{if(dirty&&!confirm('Replace unsaved graph?'))return;if(typeof projectBeforeDocumentChange==='function')projectBeforeDocumentChange();remember();doc=clone(NexgenExamples[v.name]);dirty=false;selectGraph(doc.graphs[0].name);compile();});
$('settings').onclick = () => modal('Blueprint class', [{ name: 'name', label: 'Class name', value: doc.name }], v => { if (!B.ident(v.name) || doc.variables.some(x => x.name === v.name) || doc.graphs.some(x => x.name === v.name))
    throw Error('Invalid class name'); remember(); doc.name = v.name; render(); });
$('new').onclick = () => modal('Create blueprint', [{ name: 'name', label: 'Class name', value: 'MyBlueprint' }, { name: 'kind', label: 'Parent class', options: ['Screen', 'Behaviour'] }], v => { if (!B.ident(v.name))
    throw Error('Invalid class name'); if (dirty && !confirm('Discard unsaved changes?'))
    return; if(typeof projectBeforeDocumentChange==='function')projectBeforeDocumentChange();remember(); doc = { version: 1, name: v.name, kind: v.kind, variables: [], graphs: [freshGraph('event', v.kind === 'Screen' ? 'onEnter' : 'start')] }; selectGraph(doc.graphs[0].name); });
$('addEvent').onclick = () => { const opts = Object.keys(B.events[doc.kind]).filter(n => !doc.graphs.some(g => g.name === n)); if (!opts.length)
    return alert('All events are already present'); modal('Add event', [{ name: 'name', label: 'Engine event', options: opts }], v => { remember(); doc.graphs.push(freshGraph('event', v.name)); selectGraph(v.name); }); };
$('addVar').onclick = () => modal('Add instance variable', [{ name: 'name', label: 'Name', value: 'value' }, { name: 'type', label: 'Type', options: B.types }], v => { validName(v.name); remember(); doc.variables.push({ ...v, default: B.defaultValue(v.type) }); selected = { variable: v.name }; render(); });
function parseParams(value){const ps=value.trim()?value.split(',').map(p=>{const [name,type,...rest]=p.split(':').map(x=>x.trim());if(rest.length||!B.ident(name)||!B.types.includes(type)||['then','in'].includes(name))throw Error('Use name:type for each parameter');return {name,type};}):[];if(new Set(ps.map(p=>p.name)).size!==ps.length)throw Error('Duplicate parameter');return ps;}
$('addMacro').onclick=()=>modal('Create reusable macro',[{name:'name',label:'Macro name',value:'MyMacro'},{name:'params',label:'Inputs (name:type, optional)',value:''},{name:'results',label:'Data outputs (single exit only)',value:''},{name:'exits',label:'Execution outputs (comma separated)',value:'then'}],v=>{validName(v.name);const params=parseParams(v.params),results=parseParams(v.results),exits=v.exits.split(',').map(x=>x.trim()).filter(Boolean);const g=freshGraph('macro',v.name,params);g.results=results;g.exits=exits;exits.forEach((ref,i)=>g.nodes.push({id:uid(),kind:'macroExit',ref,x:620,y:60+i*180}));B.validateMacro({...doc,graphs:[...doc.graphs,g]},g);remember();doc.graphs.push(g);selectGraph(g.name);});
$('exportLibrary').onclick=()=>{const graphs=doc.graphs.filter(g=>g.kind==='macro');if(!graphs.length)return alert('No macros to export');download(doc.name+'.bplib.json',JSON.stringify({version:1,kind:'MacroLibrary',graphs},null,2),'application/json');};
$('importLibrary').onclick=()=>$('libraryFile').click();$('libraryFile').onchange=async()=>{const f=$('libraryFile').files[0];$('libraryFile').value='';if(!f)return;try{if(f.size>2*1024*1024)throw Error('Library exceeds 2 MB');const lib=JSON.parse(await f.text());if(lib.version!==1||lib.kind!=='MacroLibrary'||!Array.isArray(lib.graphs)||lib.graphs.some(g=>g.kind!=='macro'))throw Error('Choose a macro library');const candidate=clone(doc);candidate.graphs.push(...lib.graphs);const r=B.compile(candidate);if(!r.ok)throw Error(r.diagnostics[0].message);remember();doc=candidate;render();}catch(e){alert(e.message);}};
$('addLocal').onclick=()=>{const g=graph();if(g.kind!=='function')return;modal('Add local variable',[{name:'name',label:'Name',value:'temp'},{name:'type',label:'Type',options:B.types}],v=>{if(!B.ident(v.name)||(g.locals||[]).some(x=>x.name===v.name))throw Error('Invalid or duplicate local');remember();(g.locals??=[]).push({...v,default:B.defaultValue(v.type)});selected={local:v.name};render();});};
$('addDispatcher').onclick=()=>modal('Add event dispatcher',[{name:'name',label:'Dispatcher name',value:'OnChanged'},{name:'params',label:'Parameters (name:type, optional)',value:''}],v=>{validName(v.name);const params=parseParams(v.params);remember();(doc.dispatchers??=[]).push({name:v.name,params});selected={dispatcher:v.name};render();});
$('addParent').onclick=()=>$('parentFile').click();
$('parentFile').onchange=async()=>{const f=$('parentFile').files[0];$('parentFile').value='';if(!f)return;try{if(f.size>2*1024*1024)throw Error('File exceeds 2 MB');const p=JSON.parse(await f.text());if(p.kind!==doc.kind||!B.compile(p).ok)throw Error('Choose a valid parent of the same engine base');const test=clone(doc);test.parent=p;const r=B.compile(test);if(!r.ok)throw Error('Parent cannot be applied: '+r.diagnostics[0].message);remember();doc.parent=p;selected={parent:true};render();compile();}catch(e){alert(e.message);}};
$('addFunction').onclick = () => modal('Add function / custom event', [{ name: 'name', label: 'Name', value: 'DoSomething' }, { name: 'returns', label: 'Return type (void for custom event)', options: ['void', ...B.types] }, { name: 'params', label: 'Parameters: name:type, name:type (optional)', value: '' }, {name:'purity',label:'Execution',options:['Impure','Pure (data only)']}], v => { validName(v.name); const params = v.params.trim() ? v.params.split(',').map(p => { const [name, type, ...rest] = p.split(':').map(x => x.trim()); if (rest.length || !B.ident(name) || !B.types.includes(type) || name === 'then' || name === 'in')
    throw Error('Use name:type for each parameter'); return { name, type }; }) : []; if (new Set(params.map(p => p.name)).size !== params.length)
    throw Error('Duplicate parameter'); if(v.purity==='Pure (data only)'&&v.returns==='void')throw Error('Pure functions must return a value');remember();const g=freshGraph('function',v.name,params,v.returns);if(v.purity==='Pure (data only)')g.pure=true;doc.graphs.push(g); selectGraph(v.name); });
$('deleteGraph').onclick = () => { if (doc.graphs.length === 1)
    return alert('Keep at least one graph'); if (!confirm('Delete this graph and calls to it?'))
    return; remember(); doc.graphs = doc.graphs.filter(g => g.name !== graphId); for (const g of doc.graphs) {
    const ids = new Set(g.nodes.filter(n => ['call','macroCall'].includes(n.kind) && n.ref === graphId || ['bind','unbind'].includes(n.kind)&&n.handler===graphId).map(n => n.id));
    g.nodes = g.nodes.filter(n => !ids.has(n.id));
    g.links = g.links.filter(l => !ids.has(l.from.node) && !ids.has(l.to.node));
} selectGraph(doc.graphs[0].name); };
$('addComponent').onclick = () => $('componentFile').click();
$('componentFile').onchange = async () => { const f = $('componentFile').files[0]; $('componentFile').value = ''; if (!f)
    return; try {
    if (f.size > 2 * 1024 * 1024)
        throw Error('File exceeds 2 MB');
    const c = JSON.parse(await f.text());
    if (c.kind !== 'Behaviour' || c.name === doc.name || !B.compile(c).ok)
        throw Error('Choose a valid, compiled Behaviour blueprint');
    const old = (doc.components || []).find(x => x.name === c.name);
    if(doc.sharedComponents?.includes(c.name))throw Error('This behaviour is shared. Update it from the Shared behaviours library or unlink it first.');
    if (old && !confirm('Replace component ' + c.name + '?'))
        return;
    B.interfaceRegistry({...doc,components:[...(doc.components||[]).filter(x=>x.name!==c.name),c]});
    remember();
    doc.components = (doc.components || []).filter(x => x.name !== c.name);
    doc.components.push(c);
    selected = { component: c.name };
    render();
}
catch (e) {
    alert(e.message);
} };
$('open').onclick = () => $('file').click();
$('file').onchange = async () => { const f = $('file').files[0]; $('file').value = ''; if (!f)
    return; try {
    if (f.size > 2 * 1024 * 1024)
        throw Error('File exceeds 2 MB');
    const next = JSON.parse(await f.text());
    if (next.version !== 1 || !B.events[next.kind] || !B.ident(next.name) || !Array.isArray(next.variables) || !Array.isArray(next.graphs) || !next.graphs.length || next.graphs.some(g => !g || !Array.isArray(g.nodes) || !Array.isArray(g.links)) || (next.components !== undefined && !Array.isArray(next.components)))
        throw Error('Not a version 1 blueprint');
    B.lineage(next);B.interfaceRegistry(next);B.validateHierarchies(next);
    if (dirty && !confirm('Discard unsaved changes?'))
        return;
    if(typeof projectBeforeDocumentChange==='function')projectBeforeDocumentChange();
    remember();
    doc = globalThis.NexgenSharedBehaviours?.standalone(next) || next;
    dirty = false;
    selectGraph(doc.graphs[0].name);
    compile();
}
catch (e) {
    alert(e.message);
} };
$('save').onclick = () => { download(doc.name + '.bp.json', JSON.stringify(doc, null, 2), 'application/json'); dirty = false; updateStatus(); };
$('compile').onclick = compile;
$('export').onclick = () => { const r = compile(); if (r.ok)
    download(doc.kind === 'Screen' ? 'main.as' : doc.name + '.as', r.source); };
$('undo').onclick = undo;
$('redo').onclick = redo;
$('fit').onclick = fit;
$('zoomIn').onclick = () => zoom(1.2);
$('zoomOut').onclick = () => zoom(1 / 1.2);
$('search').oninput = renderPalette;
$('context').querySelector('input').oninput = renderContext;
$('diagTab').onclick = () => { $('debugPanel').hidden=true;$('debugTab').classList.remove('active');$('diagnostics').hidden = false; $('source').hidden = true; $('copy').hidden = true; $('diagTab').classList.add('active'); $('sourceTab').classList.remove('active'); };
$('sourceTab').onclick = () => { $('debugPanel').hidden=true;$('debugTab').classList.remove('active');compile(); $('diagnostics').hidden = true; $('source').hidden = false; $('copy').hidden = false; $('sourceTab').classList.add('active'); $('diagTab').classList.remove('active'); };
$('copy').onclick = async () => { try {
    await navigator.clipboard.writeText(last?.source || '');
    $('status').textContent = 'Source copied';
}
catch {
    const r = document.createRange();
    r.selectNodeContents($('source'));
    const s = getSelection();
    s.removeAllRanges();
    s.addRange(r);
    $('status').textContent = 'Source selected; press Ctrl+C';
} };
const vp = $('viewport');
vp.oncontextmenu = e => { if (e.target.closest('#context'))
    return; e.preventDefault(); showContext(e.clientX, e.clientY); };
vp.onpointerdown = e => { if (e.target.closest('.node,#context,.wire'))
    return; hideContext(); if (e.button === 1 || space || e.button === 0) {
    e.preventDefault();
    drag = { type: 'pan', clientX: e.clientX, clientY: e.clientY, ox: view.x, oy: view.y, moved: false };
    if (e.button === 0 && !space) {
        selected = null;
        armed = null;
        renderGraph();
        renderDetails();
    }
} };
vp.onwheel = e => { if (e.target.closest('#context'))
    return; e.preventDefault(); zoom(e.deltaY < 0 ? 1.12 : 1 / 1.12, e.clientX, e.clientY); };
vp.ondragover = e => e.preventDefault();
vp.ondrop = e => { e.preventDefault(); try {
    addNode(JSON.parse(e.dataTransfer.getData('application/x-nexgen-node')), worldPoint(e.clientX, e.clientY));
}
catch { } };
window.addEventListener('pointermove', e => { if (!drag)
    return; const dx = e.clientX - drag.clientX, dy = e.clientY - drag.clientY; if (Math.abs(dx) + Math.abs(dy) > 4 && !drag.moved) {
    if (drag.type === 'node')
        remember();
    drag.moved = true;
} if (drag.type === 'node' && drag.moved) {
    drag.n.x = Math.round(drag.ox + dx / view.z);
    drag.n.y = Math.round(drag.oy + dy / view.z);
    drag.x.style.left = drag.n.x + 'px';
    drag.x.style.top = drag.n.y + 'px';
    drawWires();
}
else if (drag.type === 'pan') {
    view.x = drag.ox + dx;
    view.y = drag.oy + dy;
    transform();
}
else if (drag.type === 'wire' && drag.moved)
    drawWires(e); });
window.addEventListener('pointerup', e => { if (!drag)
    return; const a = drag; drag = null; if (a.type === 'wire') {
    const target = document.elementFromPoint(e.clientX, e.clientY)?.closest('.port');
    const b = target && portElements.get(target.dataset.port)?.data;
    if (a.moved && b) {
        const ok = connect(a.start, b), message = $('status').textContent;
        armed = null;
        render();
        if (!ok) $('status').textContent = message;
    }
    else if (a.moved && !b) {
        armed = null;
        renderGraph();
        const r = vp.getBoundingClientRect();
        if (e.clientX >= r.left && e.clientX <= r.right && e.clientY >= r.top && e.clientY <= r.bottom)
            showContext(e.clientX, e.clientY, a.start);
    }
    else if (armed && portKey(armed.node, armed.dir, armed.pin) !== portKey(a.start.node, a.start.dir, a.start.pin)) {
        const ok = connect(armed, a.start), message = $('status').textContent;
        armed = null;
        render();
        if (!ok) $('status').textContent = message;
    }
    else {
        armed = a.start;
        renderGraph();
    }
} });
window.addEventListener('keydown', e => { if (window.NexgenCommandBar?.ownsKeys(e.target)||e.target.matches('input,textarea,select,[contenteditable=true]') || document.querySelector('dialog[open]'))
    return; if (e.code === 'Space') {
    space = true;
    e.preventDefault();
} if (e.key === 'Escape') {
    armed = null;
    hideContext();
    renderGraph();
} if (e.key === 'Delete' || e.key === 'Backspace') {
    e.preventDefault();
    deleteSelected();
} if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') {
    e.preventDefault();
    e.shiftKey ? redo() : undo();
} if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
    e.preventDefault();
    $('save').click();
} });
window.addEventListener('keyup', e => { if (e.code === 'Space')
    space = false; });
function cancelDrag() {
    const wasDragging = drag;
    space = false; drag = null;
    if (wasDragging?.type === 'wire') { armed = null; renderGraph(); }
    else if (wasDragging) drawWires();
}
window.addEventListener('blur', cancelDrag);
window.addEventListener('pointercancel', cancelDrag);
window.addEventListener('beforeunload', e => { if (dirty) {
    e.preventDefault();
    e.returnValue = '';
} });
window.addEventListener('resize', drawWires);
render();
fit();
compile();
