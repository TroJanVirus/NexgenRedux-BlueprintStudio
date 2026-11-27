/* Find existing Nexgen functions without having to know their exact names. */
(function(root){'use strict';
const words=value=>String(value??'').replace(/([a-z0-9])([A-Z])/g,'$1 $2').replace(/([A-Z])([A-Z][a-z])/g,'$1 $2').toLowerCase().replace(/[^a-z0-9]+/g,' ').trim();
function search(items,query,port=null){
    const q=words(query),terms=q.split(' ').filter(Boolean),groups=new Map();
    for(const x of items){
        const d=x.d;
        if(port&&!(port.dir==='out'?d.inputs:d.outputs).some(p=>p.type===port.type))continue;
        const title=words(d.title),category=words(d.category),name=words(x.n.ref),description=words(d.description);
        const pins=words([...d.inputs,...d.outputs].map(p=>p.name+' '+p.type).join(' '));
        let score=0,matched=true;
        for(const term of terms){
            const weight=title.split(' ').includes(term)?12:title.includes(term)?8:category.includes(term)?6:name.includes(term)?5:pins.includes(term)?3:description.includes(term)?1:0;
            if(!weight){matched=false;break;}score+=weight;
        }
        if(!matched)continue;
        if(q&&title===q)score+=40;
        else if(q&&title.startsWith(q))score+=18;
        else if(q&&(category+' '+title).includes(q))score+=10;
        if(!groups.has(d.category))groups.set(d.category,[]);
        groups.get(d.category).push({x,score});
    }
    const compare=(a,b)=>b.score-a.score||a.x.d.title.localeCompare(b.x.d.title);
    const sorted=[...groups].map(([name,values])=>({name,values:values.sort(compare)}));
    sorted.sort((a,b)=>b.values[0].score-a.values[0].score||a.name.localeCompare(b.name));
    return sorted.flatMap(g=>g.values.map(v=>v.x));
}
function available(items,g){
    return items.filter(x=>!(x.n.kind==='call'&&x.n.ref===g.name)&&(!g.pure||x.n.kind==='return'||!x.d.inputs.some(p=>p.type==='exec')));
}
const api={words,search,available};if(typeof module!=='undefined'){module.exports=api;return;}root.NexgenNodeLibrary=api;
allPalette=(q,port)=>search(available(B.palette(doc,graph()),graph()),q,port);
const originalFill=fillPalette,collapsed=new Set();
const style=el('style');style.textContent='.nodeLibraryHeading{cursor:pointer;display:flex;justify-content:space-between;align-items:center}.nodeLibraryHeading:after{content:"▾"}.nodeLibraryHeading[aria-expanded=false]:after{content:"▸"}.nodeLibraryChoice{outline:1px solid var(--accent);outline-offset:-1px;background:var(--layout-control)}.nodeLibraryHelp{display:block;color:var(--accent);margin-top:10px;font-size:12px}';document.head.append(style);
fillPalette=(target,q,port,choose)=>{
    const count=originalFill(target,q,port,choose);
    for(const heading of target.querySelectorAll('.category')){
        const name=heading.textContent,rows=[];let row=heading.nextElementSibling;
        while(row&&!row.classList.contains('category')){rows.push(row);row=row.nextElementSibling;}
        heading.classList.add('nodeLibraryHeading');heading.tabIndex=0;heading.setAttribute('role','button');
        let closed=!q.trim()&&target===$('palette')&&collapsed.has(name);
        const paint=()=>{heading.setAttribute('aria-expanded',String(!closed));rows.forEach(r=>r.hidden=closed);};
        const toggle=()=>{closed=!closed;if(!q.trim()&&target===$('palette')){if(closed)collapsed.add(name);else collapsed.delete(name);}paint();};
        heading.onclick=toggle;heading.onkeydown=e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();toggle();}};paint();
    }
    return count;
};
function keyboard(input,target,context=false){
    input.addEventListener('keydown',e=>{
        if(!['ArrowDown','ArrowUp','Enter','Escape'].includes(e.key))return;
        if(e.key==='Escape'){if(context){e.preventDefault();e.stopPropagation();hideContext();$('viewport').focus();}return;}
        const rows=[...target.querySelectorAll('.item')].filter(r=>!r.hidden),i=rows.findIndex(r=>r.classList.contains('nodeLibraryChoice'));
        if(!rows.length)return;
        e.preventDefault();e.stopPropagation();
        if(e.key==='Enter'){rows[Math.max(0,i)].click();return;}
        const next=i<0?(e.key==='ArrowDown'?0:rows.length-1):(i+(e.key==='ArrowDown'?1:-1)+rows.length)%rows.length;
        rows.forEach((r,j)=>r.classList.toggle('nodeLibraryChoice',j===next));rows[next].scrollIntoView({block:'nearest'});
    });
}
keyboard($('search'),$('palette'));keyboard($('context').querySelector('input'),$('context').querySelector('div'),true);
$('search').placeholder='Search nodes, e.g. space camera';
$('context').querySelector('input').placeholder='Find a node… (↑ ↓ Enter)';
const docs='https://github.com/Team-Resurgent/NexgenRedux/blob/main/docs/ScriptReference.md';
const oldDetails=renderDetails;
renderDetails=()=>{oldDetails();const n=graph()?.nodes.find(n=>n.id===selected?.node);if(!n?.kind.startsWith('api:'))return;const d=B.describe(doc,graph(),n);if(!d)return;const a=el('a','nodeLibraryHelp','Read Nexgen reference: '+d.category);a.href=docs+'#'+d.category.toLowerCase().replace(/[^a-z0-9]+/g,'-');a.target='_blank';a.rel='noopener noreferrer';$('details').append(a);};
// The graph is built in several stages (style, compact pins and folded groups).
// Draw its wires and minimap once, after all stages have finished.
const buildGraph=renderGraph, paintWires=drawWires;
drawWires=pointer=>{if(!graphRendering)paintWires(pointer);};
renderGraph=()=>{
    if(graphRendering)return buildGraph();
    graphRendering=true;
    try{buildGraph();}finally{graphRendering=false;}
    drawWires();
};
renderPalette();
})(globalThis);
