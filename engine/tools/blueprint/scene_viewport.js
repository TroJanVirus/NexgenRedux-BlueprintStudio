/* Small 2D layout helpers. All 3D rendering and camera movement belong to Nexgen. */
(function(root){'use strict';
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
const finite=(v,d)=>Number.isFinite(v)?v:d;
const vec=(v,n,d=0)=>Array.from({length:n},(_,i)=>finite(typeof v==='number'?v:Array.isArray(v)?v[i]:undefined,d));
const snap=(v,step)=>Math.round(v/step)*step;
function color(v,f='#8eb4e6'){if(typeof v==='string'&&/^#[0-9a-f]{6}([0-9a-f]{2})?$/i.test(v))return v.slice(0,7);if(Array.isArray(v)&&v.length>=3)return '#'+v.slice(0,3).map(x=>Math.round(clamp(finite(x,0),0,1)*255).toString(16).padStart(2,'0')).join('');return f;}
function inside(x,y,poly){let yes=false;for(let i=0,j=poly.length-1;i<poly.length;j=i++){const [a,b]=poly[i],[c,d]=poly[j];if((b>y)!==(d>y)&&x<(c-a)*(y-b)/(d-b)+a)yes=!yes;}return yes;}
function pick(x,y,faces,markers){for(let i=markers.length-1;i>=0;i--){const m=markers[i];if(Math.hypot(m.x-x,m.y-y)<9)return m.index;}for(let i=faces.length-1;i>=0;i--)if(inside(x,y,faces[i].pts))return faces[i].index;return -1;}
const api={vec,snap,color,pick};if(typeof module!=='undefined')module.exports=api;else root.NexgenSceneViewport=api;
})(globalThis);
