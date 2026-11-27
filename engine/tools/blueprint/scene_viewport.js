/* Orthographic and perspective previews of native scene primitives. */
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
const near=.15;
const add=(a,b,k=1)=>a.map((x,i)=>x+b[i]*k);
function enterPerspective(v){const c=camera(v),distance=Math.max(1,v.height/(2*Math.tan(Math.PI/6)*v.scale));return {...v,perspective:true,eye:add(v.target,c.forward,distance),distance};}
function look(v,dx,dy,sensitivity=.22){return {...v,yaw:(v.yaw-dx*sensitivity)%360,pitch:clamp(v.pitch+dy*sensitivity,-89,89)};}
function fly(v,keys,dt,speed){const c=camera(v),x=(keys.has('KeyD')?1:0)-(keys.has('KeyA')?1:0),z=(keys.has('KeyW')?1:0)-(keys.has('KeyS')?1:0),y=(keys.has('KeyE')?1:0)-(keys.has('KeyQ')?1:0);const direction=add(add(c.right.map(n=>n*x),c.forward,-z),[0,y,0]);const length=Math.hypot(...direction);if(length<1e-8)return v;const amount=clamp(dt,0,.05)*speed*(keys.has('ShiftLeft')||keys.has('ShiftRight')?3:1)/Math.max(1,length);return {...v,eye:add(v.eye,direction,amount).map(n=>clamp(n,-1e6,1e6))};}
function cameraPoint(p,v){const c=camera(v),d=p.map((x,i)=>x-v.eye[i]);return [dot(d,c.right),dot(d,c.up),-dot(d,c.forward)];}
function focal(v){return v.height/(2*Math.tan(Math.PI/6));}
function screenPoint(p,v){if(p[2]<near-1e-9)return [NaN,NaN,NaN];const f=focal(v)/Math.max(near,p[2]);return [v.width/2+p[0]*f,v.height/2-p[1]*f,-p[2]];}
function project(p,v){if(v.perspective)return screenPoint(cameraPoint(p,v),v);const c=camera(v),d=p.map((x,i)=>x-v.target[i]);return [v.width/2+dot(d,c.right)*v.scale,v.height/2-dot(d,c.up)*v.scale,dot(d,c.forward)];}
function intersection(a,b){const t=(near-a[2])/(b[2]-a[2]);return [a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t,near];}
function segment(a,b,v){if(!v.perspective)return [project(a,v),project(b,v)];a=cameraPoint(a,v);b=cameraPoint(b,v);if(a[2]<near&&b[2]<near)return null;if(a[2]<near)a=intersection(a,b);else if(b[2]<near)b=intersection(a,b);return [screenPoint(a,v),screenPoint(b,v)];}
function polygon(points,v){if(!v.perspective)return points.map(p=>project(p,v));const src=points.map(p=>cameraPoint(p,v)),out=[];for(let i=0;i<src.length;i++){const a=src[(i+src.length-1)%src.length],b=src[i],inside=b[2]>=near;if((a[2]>=near)!==inside)out.push(intersection(a,b));if(inside)out.push(b);}return out.map(p=>screenPoint(p,v));}
function heightDrag(v,p,dx,dy){const scale=v.perspective?focal(v)/Math.max(near,cameraPoint(p,v)[2]):v.scale;return [0,-dy/scale,0];}

// Invert the projection at a fixed world height (also used for drag offsets).
function ground(x,y,height,v){const c=camera(v);if(v.perspective){const f=focal(v),ray=add(add(c.forward.map(n=>-n),c.right,(x-v.width/2)/f),c.up,-(y-v.height/2)/f);if(Math.abs(ray[1])<1e-7)return null;const t=(height-v.eye[1])/ray[1];return t>=near?add(v.eye,ray,t):null;}const a=(x-v.width/2)/v.scale,b=-(y-v.height/2)/v.scale,dy=height-v.target[1],det=c.right[0]*c.up[2]-c.right[2]*c.up[0];if(Math.abs(det)<1e-5)return null;const q=b-dy*c.up[1];return [v.target[0]+(a*c.up[2]-q*c.right[2])/det,height,v.target[2]+(q*c.right[0]-a*c.up[0])/det];}
const snap=(v,step)=>Math.round(v/step)*step;
function move(e,from,to,step=0){const p=vec(e.place,3);return {...e,place:p.map((v,i)=>i===1?v:step>0?snap(v+to[i]-from[i],step):v+to[i]-from[i])};}
function box(){const p=[[-.5,-.5,-.5],[.5,-.5,-.5],[.5,.5,-.5],[-.5,.5,-.5],[-.5,-.5,.5],[.5,-.5,.5],[.5,.5,.5],[-.5,.5,.5]];return [[0,1,2,3],[4,7,6,5],[0,4,5,1],[3,2,6,7],[0,3,7,4],[1,5,6,2]].map(f=>f.map(i=>p[i]));}
function rings(profile,n=12){const r=profile.map(([radius,y])=>Array.from({length:n},(_,i)=>[radius*Math.cos(i*2*Math.PI/n),y,radius*Math.sin(i*2*Math.PI/n)])),out=[];for(let j=0;j<r.length-1;j++)for(let i=0;i<n;i++)out.push([r[j][i],r[j][(i+1)%n],r[j+1][(i+1)%n],r[j+1][i]]);return out;}
const meshes={box:box(),plane:[[[-.5,0,-.5],[.5,0,-.5],[.5,0,.5],[-.5,0,.5]]],cylinder:rings([[0,-.5],[.5,-.5],[.5,.5],[0,.5]]),cone:rings([[0,-.5],[.5,-.5],[0,.5]]),sphere:rings(Array.from({length:9},(_,i)=>[.5*Math.sin(i*Math.PI/8),-.5*Math.cos(i*Math.PI/8)])),capsule:rings([...Array.from({length:5},(_,i)=>[.5*Math.sin(i*Math.PI/8),-.5*Math.cos(i*Math.PI/8)-.5]),...Array.from({length:5},(_,i)=>[.5*Math.sin((i+4)*Math.PI/8),-.5*Math.cos((i+4)*Math.PI/8)+.5])]),torus:rings(Array.from({length:13},(_,i)=>[.35+.15*Math.cos(i*2*Math.PI/12),.15*Math.sin(i*2*Math.PI/12)]))};
function color(v,f='#8eb4e6'){if(typeof v==='string'&&/^#[0-9a-f]{6}([0-9a-f]{2})?$/i.test(v))return v.slice(0,7);if(Array.isArray(v)&&v.length>=3)return '#'+v.slice(0,3).map(x=>Math.round(clamp(finite(x,0),0,1)*255).toString(16).padStart(2,'0')).join('');return f;}
function faces(e,v,index,simplify=false){const placeholder=!!(e.model||e.sprite),mesh=placeholder||simplify&&e.shape&&e.shape!=="none"&&e.shape!=="plane"?meshes.box:meshes[e.shape];if(!mesh)return [];const base=color(e.color);return mesh.map((f,i)=>{const pts=polygon(f.map(p=>world(e,p)),v);const depth=pts.reduce((s,p)=>s+p[2],0)/pts.length;return {pts,depth,index,placeholder,base,shade:[.73,.85,.62,1,.91,.79][i%6]};}).filter(f=>f.pts.length>=3&&f.pts.every(p=>p.every(Number.isFinite)));}
function inside(x,y,poly){let yes=false;for(let i=0,j=poly.length-1;i<poly.length;j=i++){const [a,b]=poly[i],[c,d]=poly[j];if((b>y)!==(d>y)&&x<(c-a)*(y-b)/(d-b)+a)yes=!yes;}return yes;}
function pick(x,y,faces,markers){for(let i=markers.length-1;i>=0;i--){const m=markers[i];if(Math.hypot(m.x-x,m.y-y)<9)return m.index;}for(let i=faces.length-1;i>=0;i--)if(inside(x,y,faces[i].pts))return faces[i].index;return -1;}
const api={shapes,vec,angles,rotate,world,project,ground,move,snap,color,faces,pick,enterPerspective,look,fly,segment,heightDrag,cameraPoint};if(typeof module!=='undefined')module.exports=api;else root.NexgenSceneViewport=api;
})(globalThis);
