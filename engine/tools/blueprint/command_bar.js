/* Keep the common commands visible and put the rest in small, keyboard-friendly menus. */
(function(){'use strict';
const toolbar=document.querySelector('header .toolbar'),graphbar=document.querySelector('.graphbar>div');
if(!toolbar||!graphbar||typeof HTMLElement.prototype.showPopover!=='function'){window.NexgenCommandBar={setExpanded(){},ownsKeys(){return false;},expanded:true};return;}
const original=new Map([[toolbar,[...toolbar.children]],[graphbar,[...graphbar.children]]]);
const groups=[];let expanded=false;
const style=el('style');style.textContent=`
.commandCompact header .toolbar{flex-wrap:nowrap;justify-content:flex-start;overflow:auto;min-width:0;scrollbar-width:thin}
.commandCompact header .toolbar button{flex-shrink:0}
.commandCompact .graphbar{overflow:auto;scrollbar-width:thin}
.commandCompact .graphbar #graphName{min-width:72px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.commandCompact .graphbar>div{flex-shrink:0}
.commandMenu[popover]{position:fixed;inset:auto;margin:0;padding:5px;max-width:calc(100vw - 12px);max-height:calc(100vh - 12px);overflow:auto;border:1px solid var(--line);border-radius:6px;background:var(--layout-panel,var(--panel));color:var(--layout-panelText,var(--text));box-shadow:0 5px 22px #0007;min-width:160px;z-index:20}
.commandMenu button{display:block;text-align:left;width:100%;white-space:normal;margin:2px 0;padding:7px 10px;font-size:12px}
.commandMenu button:focus-visible,.commandTrigger:focus-visible{outline:2px solid var(--accent);outline-offset:1px}
.commandTrigger[aria-expanded=true]{border-color:var(--accent)}
.commandCompact header .brand{flex-shrink:0}
@media(max-width:1200px){.commandCompact header .brand small{display:none}.commandCompact header #status{display:none}}
`;document.head.append(style);
const matches=(b,keys)=>keys.some(k=>k[0]==='#'?b.id===k.slice(1):b.textContent.trim()===k);
function menu(parent,name,keys){const buttons=keys.map(k=>original.get(parent).find(b=>b.tagName==='BUTTON'&&matches(b,[k]))).filter(Boolean);if(!buttons.length)return;const trigger=el('button','commandTrigger',name+' ▾');trigger.type='button';trigger.setAttribute('aria-expanded','false');trigger.setAttribute('aria-label',name+' commands');const popup=el('div','commandMenu');popup.id='command-menu-'+groups.length;popup.setAttribute('popover','auto');popup.setAttribute('aria-label',name+' commands');trigger.setAttribute('aria-controls',popup.id);trigger.setAttribute('popovertarget',popup.id);parent.append(popup);const entry={parent,buttons,trigger,popup};groups.push(entry);
 function place(){const r=trigger.getBoundingClientRect(),w=popup.offsetWidth,h=popup.offsetHeight;popup.style.left=Math.max(6,Math.min(r.left,innerWidth-w-6))+'px';popup.style.top=Math.max(6,Math.min(r.bottom+4,innerHeight-h-6))+'px';}
 function show(focus=false){for(const g of groups)if(g!==entry&&g.popup.matches(':popover-open'))g.popup.hidePopover();if(!popup.matches(':popover-open'))popup.showPopover();place();trigger.setAttribute('aria-expanded','true');if(focus)popup.querySelector('button:not(:disabled):not([hidden])')?.focus();}
 trigger.onkeydown=e=>{if(e.key==='ArrowDown'||e.key==='ArrowUp'){e.preventDefault();show(true);}};
 popup.addEventListener('toggle',()=>{const open=popup.matches(':popover-open');trigger.setAttribute('aria-expanded',String(open));if(open)place();});
 popup.addEventListener('click',e=>{if(e.target.closest('button'))popup.hidePopover();},true);
 popup.addEventListener('keydown',e=>{const bs=[...popup.querySelectorAll('button:not(:disabled):not([hidden])')];if(e.key==='Escape'){e.preventDefault();popup.hidePopover();trigger.focus();}else if(['ArrowDown','ArrowUp','Home','End'].includes(e.key)&&bs.length){e.preventDefault();const i=bs.indexOf(document.activeElement);bs[e.key==='Home'?0:e.key==='End'?bs.length-1:e.key==='ArrowDown'?(i+1)%bs.length:(i<0?bs.length-1:(i+bs.length-1)%bs.length)].focus();}});
 return entry;
}
const file=menu(toolbar,'File',['#new','#open','#examplesButton','#export','#exportProject']);
menu(toolbar,'Game',['#projectManager','#sceneEditorButton','#uiDesignerButton','#sharedBehavioursButton','Assets','Check project']);
menu(toolbar,'Development',['#debugTabButton','#recoveryButton']);
menu(graphbar,'View',['Focus','Layout','Graph image','Bookmarks']);
const expand=el('button','','Compact toolbars');expand.type='button';expand.id='compactCommands';expand.title='Show only common toolbar commands';expand.onclick=()=>setExpanded(false);
const all=el('button','','Show all toolbar commands');all.type='button';all.onclick=()=>setExpanded(true);
function setExpanded(on){expanded=!!on;for(const g of groups)if(g.popup.matches(':popover-open'))g.popup.hidePopover();document.body.classList.toggle('commandCompact',!expanded);
 for(const [parent,children] of original){const own=groups.filter(g=>g.parent===parent);for(const g of own)g.trigger.remove();for(const child of children){child.hidden===true&&child.classList.contains('sep')&&(child.hidden=false);parent.append(child);}if(!expanded){for(const g of own){const first=g.buttons[0];parent.insertBefore(g.trigger,first);g.popup.replaceChildren(...g.buttons);parent.append(g.popup);}for(const child of children)if(child.classList.contains('sep'))child.hidden=true;}}
 if(expanded){toolbar.append(expand);all.remove();}else{expand.remove();file?.popup.append(all);}try{localStorage.setItem('nexgen-command-bar',expanded?'expanded':'compact');}catch{} }
window.addEventListener('resize',()=>{for(const g of groups)if(g.popup.matches(':popover-open'))g.popup.hidePopover();});
let saved=false;try{saved=localStorage.getItem('nexgen-command-bar')==='expanded';}catch{}setExpanded(saved);
window.NexgenCommandBar={setExpanded,ownsKeys(target){return !!target?.closest?.('.commandMenu,.commandTrigger,#compactCommands')||groups.some(g=>g.popup.matches(':popover-open'));},get expanded(){return expanded;}};
})();
