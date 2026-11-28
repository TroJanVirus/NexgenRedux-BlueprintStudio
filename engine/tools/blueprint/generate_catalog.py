#!/usr/bin/env python3
"""Generate the visual node catalogue from Nexgen's checked-in script reference."""
import argparse, json, re
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
text = (ROOT / 'docs/ScriptReference.md').read_text(encoding='utf-8')
handles = {'Texture','Model','Effect','Mesh','Font','Sound','Video','Look','Canvas','Entity','Widget','Room','Message','Request','Connection','Datagrams','Json','Path','Playlist','WebSocket','Pixels','Random','Thread','Channel','Samples','AudioStream','Curve','Batch','Packed','Zip'}
values = {'bool','int','uint','float','double','string','Vec2','Vec3','Color','Blend','Filter','Align','Ease','Bus','FieldShape','FieldJoin','Solid','Place','RoomState','Voice','Tween','Tray','XboxText','Pointer'}
sections = handles | {'Assets','Input','Audio','Save','Scene','Space','Screens','App','Maths','Ui','Net','Paths and noise','Vec2','Vec3','Color','Coroutines','Voice','Tween','Xbox','Xbox360','XboxOne','Windows','Mac','Linux','Data','Window','System'}

def split(s):
    parts=[]; start=0; depth=0; quote=False; escape=False
    for i,c in enumerate(s):
        if escape: escape=False; continue
        if c=='\\' and quote: escape=True; continue
        if c=='"': quote=not quote
        if not quote:
            if c in '(<': depth+=1
            if c in ')>': depth-=1
            if c==',' and depth==0: parts.append(s[start:i].strip()); start=i+1
    if s.strip(): parts.append(s[start:].strip())
    return parts

def parse_type(s):
    s=re.sub(r'\bconst\s+|\s*&in\b|@\+',lambda m:'@' if m.group()=='@+' else '',s).strip()
    if s in values or s in {h+'@' for h in handles}: return s
    am=re.fullmatch(r'array<([\w@]+)>@',s)
    if am and (am[1] in values or am[1] in {h+'@' for h in handles}): return s
    return None

def default(t,s):
    if s is None: return None
    if t=='string' and s.startswith('"'):
        try: return json.loads(s)
        except ValueError: return None
    if t=='bool': return s=='true' if s in ('true','false') else None
    if t in ('int','uint','float','double'):
        try: return int(s,0) if t in ('int','uint') else float(s.rstrip('f'))
        except ValueError: return None
    if t in ('Vec2','Vec3','Color'):
        if t=='Color' and s in ('White','Black','Red','Orange','Yellow','Green','Blue','Purple','Gray'): return s
        if s.startswith(t+'(') and s.endswith(')'):
            try:
                result=[float(x.rstrip('f')) for x in split(s[len(t)+1:-1])]
                if not result: return [0,0] if t=='Vec2' else [0,0,0] if t=='Vec3' else [0,0,0,1]
                if t=='Color' and len(result)==3: result.append(1)
                return result
            except ValueError: return None
    if t in values and re.fullmatch(r'[A-Za-z_]\w*',s): return s
    return None

nodes=[]; seen=set(); section=''
for line in text.splitlines():
    if line.startswith('## ' ) or line.startswith('### '): section=line.split(' ',1)[1]
    if section not in sections: continue
    m=re.match(r'\| `([^`]+)` \| (.*?) \|$',line)
    if not m: continue
    sig,desc=m.groups()
    m=re.fullmatch(r'([\w@<>]+) ([\w:]+)\((.*)\)(?: const)?( property)?',sig)
    if not m: continue
    ret,name,args,prop=m.groups(); ret='void' if ret=='void' else parse_type(ret)
    # Blocking worker-only operations must not become ordinary game-thread nodes.
    if (section == 'Thread' and name in ('wait','Worker::sleep','Worker::get_stopping','Worker::get_inThread')) or (section == 'Channel' and name == 'demand'): continue
    if not ret or (section in {'Vec3','Color'} and not '::' in name and name not in ('hsv','lerp','fade','toLinear','toGamma')): continue
    target=(section+'@' if section in handles else section) if section in handles|{'Voice','Tween','Vec2'} and '::' not in name else None
    if not target and '::' not in name and section not in ('Maths','Color','Paths and noise'): continue
    if section=='Tween' and name=='ease': target=None
    if section in ('Path','Curve') and name in ('noise','fractalNoise','noiseSeed','triangulate','isConvex'): target=None
    if section=='Random' and name=='randomNormal': target=None
    pins=[]; outputs=[]; arguments=[]; valid=True
    if target: pins.append({'name':'target','type':target})
    for i,arg in enumerate(split(args)):
        a,eq,d=arg.partition('='); a=a.strip(); d=d.strip() if eq else None
        isout='&out' in a
        if '&inout' in a: valid=False; break
        a=re.sub(r'(array<[\w@]+>)\s*&in\b',r'\1@',a)
        a=re.sub(r'\bconst\s+|\s*&(?:in|out)\b|@\+',lambda m:'@' if m.group()=='@+' else '',a).strip()
        pm=re.fullmatch(r'([\w@<>]+)(?:\s+(\w+))?',a)
        if not pm or not parse_type(pm[1]): valid=False; break
        t=pm[1]; p=pm[2] or ('value' if prop and name.split('::')[-1].startswith('set_') else 'arg'+str(i+1))
        if any(x['name']==p for x in pins+outputs) or p in ('in','then','result'): p='arg'+str(i+1)
        pin={'name':p,'type':t}; val=default(t,d)
        if val is not None: pin['default']=val
        elif d is not None and d!='null': valid=False; break
        if isout: outputs.append(pin)
        else: pins.append(pin)
        arguments.append({'name':p,'out':isout,'type':t})
    if not valid: continue
    short=name.split('::')[-1]; isget=bool(prop and short.startswith('get_')); isset=bool(prop and short.startswith('set_'))
    if prop and not (isget or isset): continue
    if isset and len(pins)!=(2 if target else 1): continue
    key=(section,name,tuple(p['type'] for p in pins),tuple((p['type'],p['name']) for p in outputs))
    if key in seen: continue
    seen.add(key)
    # Calls with side effects (including random and asset allocation) get execution pins.
    pure=not outputs and (isget or section=='Vec2' or (section in ('Maths','Color') and not short.startswith('random')) or (section=='Input' and short in ('held','pressed','released','repeated','key','mouseDown','padConnected','padLeft','padRight','padTriggers','padButton','keys','buttons')))
    label=(('Get ' if isget else 'Set ' if isset else '')+short[4:] if isget or isset else short)
    nodes.append({'id':'api:'+section+':'+name+':'+','.join(p['type'] for p in pins)+(':'+','.join(p['type']+' out' for p in outputs) if outputs else ''),'title':label,'category':section,'description':desc,'inputs':pins,'outputs':outputs,'arguments':arguments,'result':ret,'pure':pure,'call':name,'member':bool(target),'property':'get' if isget else 'set' if isset else ''})
# Factory declarations are registered by the engine but not emitted as reference table rows.
# Keep these signatures aligned with bind_threads.cpp and bindings.cpp.
for category, title, inputs in [
    ('Curve', 'Create Bezier curve', []),
    ('Curve', 'Create Bezier curve from points', [{'name':'points','type':'array<Vec2>@'}]),
    ('Batch', 'Create untextured sprite batch', []),
    ('Batch', 'Create textured sprite batch', [{'name':'texture','type':'Texture@'}]),
    ('Packed', 'Create packed data', []),
    ('Packed', 'Open packed bytes', [{'name':'bytes','type':'string'}]),
    ('Zip', 'Create ZIP archive', []),
    ('Zip', 'Open ZIP bytes', [{'name':'file','type':'string'}]),
    ('Thread', 'Create worker thread', [{'name':'script','type':'string'}]),
    ('Channel', 'Get named channel', [{'name':'name','type':'string'}]),
    ('Samples', 'Create samples', [{'name':'frames','type':'uint'}, {'name':'rate','type':'uint','default':48000}, {'name':'channels','type':'uint','default':1}]),
    ('AudioStream', 'Create audio stream', [{'name':'rate','type':'uint','default':48000}, {'name':'channels','type':'uint','default':1}, {'name':'seconds','type':'float','default':0.25}]),
]:
    nodes.append({'id':'api:'+category+':'+category+':'+','.join(p['type'] for p in inputs),'title':title,'category':category,'description':'Creates a native Nexgen '+category+' handle.','inputs':inputs,'outputs':[],'arguments':[{'name':p['name'],'out':False,'type':p['type']} for p in inputs],'result':category+'@','pure':False,'call':category,'member':False,'property':''})
# Old projects stored the signature in their node IDs. Keep the old paragraph node;
# Redux's new Align parameter has a default, so existing emitted calls still work.
for n in list(nodes):
    if n['category']=='Canvas' and n['call']=='paragraph' and n['inputs'][-1]['type']=='Align':
        old=dict(n, id=n['id'].removesuffix(',Align'), inputs=n['inputs'][:-1], arguments=n['arguments'][:-1], title='paragraph (legacy)')
        nodes.append(old)
# Enums are collected from the same reference, not inferred from name strings.
enums={}
for name in values-{'bool','int','uint','float','double','string','Vec2','Vec3','Color','Voice','Tween'}:
    match=re.search(r'^## '+name+r'\n(.*?)(?=^## |\Z)',text,re.M|re.S)
    if match:
        enums[name]=re.findall(r'^\| `([A-Za-z_]\w*)` \|',match[1],re.M)
nodes=[n for n in nodes if all(p['type'] not in enums or enums[p['type']] for p in n['inputs']) and (n['result'] not in enums or enums[n['result']])]
# Drop unsupported enum types, e.g. if their declarations aren't listed as individual tokens.
known={'bool','int','uint','float','double','string','Vec2','Vec3','Color','Voice','Tween','void'}|{h+'@' for h in handles}|{k for k,v in enums.items() if v}
def supported(t):
    return t in known or (t.startswith('array<') and t.endswith('>@') and t[6:-2] in known)
nodes=[n for n in nodes if supported(n['result']) and all(supported(p['type']) for p in n['inputs']+n['outputs'])]
generated=('// Generated by generate_catalog.py; do not hand edit.\n(function(root){const catalog='+json.dumps({'nodes':nodes,'enums':{k:v for k,v in enums.items() if v}},ensure_ascii=False,sort_keys=True,separators=(',',':'))+';if(typeof module!=="undefined")module.exports=catalog;else root.NexgenCatalog=catalog;})(globalThis);\n')
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true', help='Check that the checked-in catalogue matches the reference without writing files')
args=parser.parse_args()
output=HERE/'catalog.js'
if args.check:
    if not output.exists() or output.read_text(encoding='utf-8') != generated:
        parser.exit(1, 'Node catalogue is out of date. Run generate_catalog.py and make_examples.js.\n')
else:
    output.write_text(generated, encoding='utf-8')
print(f'{len(nodes)} API nodes' + (' (checked)' if args.check else ''))
