/* Import existing Nexgen screen folders without rewriting unconverted scripts. */
(function(root){'use strict';
const I=typeof module!=='undefined'?require('./script_import'):root.NexgenScriptImport;
const utf8=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true});
const copy=x=>JSON.parse(JSON.stringify(x));
function read(P,files){
 if(!Array.isArray(files)||!files.length||files.length>4096)throw Error('Choose up to 4096 project files.');
 P.validateAssets([{assets:files}]);
 // webkitRelativePath includes the selected directory. Accept a screen, showcase, media or a project containing media.
 const groups=new Map(),global=[],ignored=[];
 for(const f of files){const parts=f.path.split('/');let i=parts.indexOf('showcase');if(i>=0&&(i===0||parts[i-1]==='media')&&parts.length>i+2){const id=parts[i+1];if(!P.isId(id))throw Error('Unsupported screen ID: '+id);if(!groups.has(id))groups.set(id,{id,files:[],root:parts.slice(0,i+2).join('/')});const g=groups.get(id);if(g.root!==parts.slice(0,i+2).join('/'))throw Error('Multiple folders use screen ID '+id);g.files.push({path:parts.slice(i+2).join('/'),data:f.data});continue;}
 const j=parts.indexOf('global');if(j>=0&&parts.length>j+1&&(j===0||parts[j-1]==='media')){global.push({path:parts.slice(j+1).join('/'),data:f.data});continue;}
 ignored.push(f);
 }
 if(!groups.size){const mains=ignored.filter(f=>f.path.endsWith('/main.as'));if(mains.length!==1)throw Error('Select a screen folder, showcase folder, or project containing media/showcase.');const prefix=mains[0].path.slice(0,-'main.as'.length),id=prefix.split('/').filter(Boolean).at(-1);if(!P.isId(id))throw Error('Unsupported screen ID: '+id);const local=ignored.filter(f=>f.path.startsWith(prefix));groups.set(id,{id,files:local.map(f=>({path:f.path.slice(prefix.length),data:f.data}))});for(const f of local)ignored.splice(ignored.indexOf(f),1);}
 if(groups.size>128)throw Error('Projects support up to 128 screens.');
 const screens=[];for(const g of groups.values()){const main=g.files.find(f=>f.path==='main.as'),meta=g.files.find(f=>f.path==='screen.json');if(!main||!meta)throw Error(g.id+': main.as and screen.json are required.');let m;try{m=JSON.parse(utf8.decode(P.decode(meta.data)).replace(/^\uFEFF/,''));}catch{throw Error(g.id+': invalid screen.json');}if(!m||typeof m.title!=='string'||!m.title.trim()||m.title.length>128)throw Error(g.id+': invalid screen title');let candidate=null,reason='';try{const s=utf8.decode(P.decode(main.data));if(P.decode(main.data).length>256*1024)throw Error('Script exceeds the conversion limit');candidate=I.read(s,'main.as',{mixed:true});if(candidate.doc.kind!=='Screen')throw Error('The main class is not a Screen');}catch(e){candidate=null;reason=e.message;}screens.push({...g,title:m.title,candidate,reason});}
 return {screens,global,ignored:ignored.map(f=>f.path)};
}
function build(P,review,ids,name){const chosen=new Set(ids);if(!chosen.size)throw Error('Choose at least one convertible screen to edit.');const project={version:1,kind:'BlueprintProject',name,screens:[],nativeScreens:[],nativeGlobal:{assets:copy(review.global)}};
 for(const s of review.screens){if(chosen.has(s.id)){if(!s.candidate)throw Error('Cannot convert '+s.id);project.screens.push({id:s.id,title:s.title,blueprint:copy(s.candidate.doc),assets:s.files.filter(f=>f.path.startsWith('asset/')).map(f=>({path:f.path.slice(6),data:f.data})),nativeFiles:s.files.filter(f=>f.path!=='main.as'&&!f.path.startsWith('asset/')),originalMain:s.files.find(f=>f.path==='main.as').data});}else project.nativeScreens.push({id:s.id,title:s.title,assets:copy(s.files)});}
 if(project.screens.length!==chosen.size)throw Error('Unknown conversion selection.');P.validateProject(project);return project;
}
const api={read,build};if(typeof module!=='undefined')module.exports=api;else root.NexgenFolderImport=api;
})(globalThis);
