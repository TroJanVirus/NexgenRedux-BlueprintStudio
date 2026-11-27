"""Exercise a large graph, and check that wire elements are reused correctly."""
from pathlib import Path
from playwright.sync_api import sync_playwright
import os, subprocess, tempfile

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html = Path(tmp) / 'studio.html'
    subprocess.run(['node', str(root/'bundle.js'), str(html)], check=True)
    with sync_playwright() as pw:
        browser = pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'), args=['--no-sandbox', '--disable-dev-shm-usage'])
        page = browser.new_page(viewport={'width':1600, 'height':900})
        errors = []
        page.on('pageerror', lambda e: errors.append(str(e)))
        page.set_content(html.read_text())
        result = page.evaluate('''() => {
            const g = graph();
            g.nodes = [{id:'start',kind:'entry',x:0,y:0}];
            g.links = [];
            for (let i=0; i<400; i++) {
                g.nodes.push({id:'n'+i,kind:'print',x:280*(i%20),y:160*Math.floor(i/20)});
                g.links.push({from:{node:i?'n'+(i-1):'start',pin:'then'},to:{node:'n'+i,pin:'in'}});
            }
            selected=null;
            renderGraph();
            const source=B.compile(doc).source;
            if (!B.compile(doc).ok) throw Error('Large graph does not compile');
            const wires=$('wires'), first=wires.firstElementChild, last=wires.lastElementChild;
            const before=first.getAttribute('d'), lastBefore=last.getAttribute('d');
            g.nodes[1].x+=60;
            $('node-n0').style.left=g.nodes[1].x+'px';
            drawWires();
            if(wires.firstElementChild!==first||wires.lastElementChild!==last||first.getAttribute('d')===before||last.getAttribute('d')!==lastBefore)throw Error('Wire reuse or movement failed');
            renderGraph();
            if(wires.firstElementChild!==first||wires.children.length!==400)throw Error('Graph redraw did not retain wires');
            if(B.compile(doc).source!==source)throw Error('Layout changed compiled code');
            // Fold a chain and verify that only its boundary wires remain visible.
            g.frames=[{title:'Chain',color:'#426a8e',nodes:['n1','n2','n3'],collapsed:true}];
            renderGraph();
            if(wires.children.length!==398)throw Error('Folded wire count');
            g.frames[0].collapsed=false;renderGraph();
            if(wires.children.length!==400)throw Error('Expanded wire count');
            const l=g.links[0];g.links.shift();renderGraph();
            if(first.isConnected||wires.children.length!==399)throw Error('Deleted wire was retained');
            g.links.unshift(l);renderGraph();
            if(wires.children.length!==400)throw Error('Restored wire missing');
            if(B.compile(doc).source!==source)throw Error('Wire redraw changed compiled code');
            return {nodes:g.nodes.length,wires:wires.children.length};
        }''')
        assert result == {'nodes':401,'wires':400},result
        assert not errors, errors
        browser.close()
        print('PASS large graph, stable wire elements, fold/expand, deletion, restoration and unchanged compiled source')
