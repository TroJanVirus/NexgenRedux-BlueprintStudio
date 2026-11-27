/* Small orthographic preview of native scene primitives. No engine code runs here. */
(function(root){'use strict';
const shapes=['none','box','sphere','cylinder','cone','torus','capsule','plane'];
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
const finite=(v,d)=>Number.isFinite(v)?v:d;
const vec=(v,n,d=0)=>Array.from({length:n},(_,i)=>finite(typeof v==='number'?v:Array.isArray(v)?v[i]:undefined,d));
const angles=e=>typeof e.angles==='number'?[0,e.angles,0]:vec(e.angles,3);
const radians=v=>(v%360)*Math.PI/180;
function rotate(p,a){const [pitch,yaw,roll]=a.map(radians),[x,y,z]=p;const cr=Math.cos(roll),sr=Math.sin(roll),cp=Math.cos(pitch),sp=Math.sin(pitch),cy=Math.cos(yaw),sy=Math.sin(yaw);const X=x*cr-y*sr,Y=x*sr+y*cr,Z=z;const Y2=Y*cp-Z*sp,Z2=Y*sp+Z*cp;return [X*cy+Z2*sy,Y2,-X*sy+Z2*cy];}
function world(e,p){const s=vec(e.size,3,1),a=rotate(p.map((v,i)=>v*s[i]),angles(e)),t=vec(e.place,3);return a.map((v,i)=>v+t[i]);}
function camera(view){const a=radians(view.yaw),b=radians(view.pitch);return {right:[Math.cos(a),0,-Math.sin(a)],up:[-Math.sin(a)*Math.sin(b),Math.cos(b),-Math.cos(a)*Math.sin(b)],forward:[Math.sin(a)*Math.cos(b),Math.sin(b),Math.cos(a)*Math.cos(b)]};}
const dot=(a,b)=>a.reduce((v,x,i)=>v+x*b[i],0);
function project(p,v){const c=camera(v),d=p.map((x,i)=>x-v.target[i]);return [v.width/2+dot(d,c.right)*v.scale,v.height/2-dot(d,c.up)*v.scale,dot(d,c.forward)];}
// Invert the projection at a fixed world height (also used for drag offsets).
function ground(x,y,height,v){const c=camera(v),a=(x-v.width/2)/v.scale,b=-(y-v.height/2)/v.scale,dy=height-v.target[1],det=c.right[0]*c.up[2]-c.right[2]*c.up[0];if(Math.abs(det)<1e-5)return null;const q=b-dy*c.up[1];return [v.target[0]+(a*c.up[2]-q*c.right[2])/det,height,v.target[2]+(q*c.right[0]-a*c.up[0])/det];}
const snap=(v,step)=>Math.round(v/step)*step;
function move(e,from,to,step=0){const p=vec(e.place,3);return {...e,place:p.map((v,i)=>i===1?v:step>0?snap(v+to[i]-from[i],step):v+to[i]-from[i])};}
function box(){const p=[[-.5,-.5,-.5],[.5,-.5,-.5],[.5,.5,-.5],[-.5,.5,-.5],[-.5,-.5,.5],[.5,-.5,.5],[.5,.5,.5],[-.5,.5,.5]];return [[0,1,2,3],[4,7,6,5],[0,4,5,1],[3,2,6,7],[0,3,7,4],[1,5,6,2]].map(f=>f.map(i=>p[i]));}
function rings(profile,n=12){const r=profile.map(([radius,y])=>Array.from({length:n},(_,i)=>[radius*Math.cos(i*2*Math.PI/n),y,radius*Math.sin(i*2*Math.PI/n)])),out=[];for(let j=0;j<r.length-1;j++)for(let i=0;i<n;i++)out.push([r[j][i],r[j][(i+1)%n],r[j+1][(i+1)%n],r[j+1][i]]);return out;}
const meshes={box:box(),plane:[[[-.5,0,-.5],[.5,0,-.5],[.5,0,.5],[-.5,0,.5]]],cylinder:rings([[0,-.5],[.5,-.5],[.5,.5],[0,.5]]),cone:rings([[0,-.5],[.5,-.5],[0,.5]]),sphere:rings(Array.from({length:9},(_,i)=>[.5*Math.sin(i*Math.PI/8),-.5*Math.cos(i*Math.PI/8)])),capsule:rings([...Array.from({length:5},(_,i)=>[.5*Math.sin(i*Math.PI/8),-.5*Math.cos(i*Math.PI/8)-.5]),...Array.from({length:5},(_,i)=>[.5*Math.sin((i+4)*Math.PI/8),-.5*Math.cos((i+4)*Math.PI/8)+.5])]),torus:rings(Array.from({length:13},(_,i)=>[.35+.15*Math.cos(i*2*Math.PI/12),.15*Math.sin(i*2*Math.PI/12)]))};
function color(v,f='#8eb4e6'){if(typeof v==='string'&&/^#[0-9a-f]{6}([0-9a-f]{2})?$/i.test(v))return v.slice(0,7);if(Array.isArray(v)&&v.length>=3)return '#'+v.slice(0,3).map(x=>Math.round(clamp(finite(x,0),0,1)*255).toString(16).padStart(2,'0')).join('');return f;}
function faces(e,v,index,simplify=false){const placeholder=!!(e.model||e.sprite),mesh=placeholder||simplify&&e.shape&&e.shape!=="none"&&e.shape!=="plane"?meshes.box:meshes[e.shape];if(!mesh)return [];const base=color(e.color);return mesh.map((f,i)=>{const pts=f.map(p=>project(world(e,p),v));const depth=pts.reduce((s,p)=>s+p[2],0)/pts.length;return {pts,depth,index,placeholder,base,shade:[.73,.85,.62,1,.91,.79][i%6]};}).filter(f=>f.pts.every(p=>p.every(Number.isFinite)));}
function inside(x,y,poly){let yes=false;for(let i=0,j=poly.length-1;i<poly.length;j=i++){const [a,b]=poly[i],[c,d]=poly[j];if((b>y)!==(d>y)&&x<(c-a)*(y-b)/(d-b)+a)yes=!yes;}return yes;}
function pick(x,y,faces,markers){for(let i=markers.length-1;i>=0;i--){const m=markers[i];if(Math.hypot(m.x-x,m.y-y)<9)return m.index;}for(let i=faces.length-1;i>=0;i--)if(inside(x,y,faces[i].pts))return faces[i].index;return -1;}
const api={shapes,vec,angles,rotate,world,project,ground,move,snap,color,faces,pick};if(typeof module!=='undefined')module.exports=api;else root.NexgenSceneViewport=api;
})(globalThis);
