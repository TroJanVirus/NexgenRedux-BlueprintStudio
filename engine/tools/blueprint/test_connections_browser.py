"""Exercise actual pin clicks, including a rejected cycle and a valid replacement."""
from pathlib import Path
from playwright.sync_api import sync_playwright
import os, subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html'
    subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage'])
        page=browser.new_page(viewport={'width':1600,'height':1000})
        errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
        page.set_content(html.read_text())
        page.evaluate('''()=>{doc={version:1,name:'BP_Wires',kind:'Screen',variables:[],graphs:[{name:'onEnter',kind:'event',nodes:[{id:'entry',kind:'entry',x:0,y:20},{id:'a',kind:'print',x:300,y:20},{id:'b',kind:'print',x:600,y:20},{id:'c',kind:'print',x:900,y:20},{id:'x',kind:'add',type:'int',x:300,y:300},{id:'y',kind:'add',type:'int',x:600,y:300}],links:[{from:{node:'entry',pin:'then'},to:{node:'a',pin:'in'}},{from:{node:'a',pin:'then'},to:{node:'b',pin:'in'}},{from:{node:'b',pin:'then'},to:{node:'c',pin:'in'}},{from:{node:'x',pin:'result'},to:{node:'y',pin:'a'}}]}]};graphId='onEnter';history=[];future=[];selected=null;armed=null;render();fit()}''')
        def click(node,direction,pin):
            page.locator(f'[data-port="{node}|{direction}|{pin}"]').click()
        initial=page.evaluate('JSON.stringify(graph().links)')
        click('b','out','then');click('a','in','in')
        assert 'Execution cycle' in page.locator('#status').inner_text()
        assert page.evaluate('JSON.stringify(graph().links)')==initial
        assert page.evaluate('history.length')==0
        click('y','out','result');click('x','in','a')
        assert 'Data cycle' in page.locator('#status').inner_text()
        assert page.evaluate('JSON.stringify(graph().links)')==initial
        assert page.evaluate('history.length')==0
        click('a','out','then');click('b','in','in')
        assert page.evaluate('history.length')==0
        click('a','out','then');click('c','in','in')
        assert page.evaluate('history.length')==1
        assert page.evaluate('graph().links.some(l=>l.from.node==="a"&&l.to.node==="c")')
        page.locator('#undo').click()
        assert page.evaluate('JSON.stringify(graph().links)')==initial
        assert not errors,errors
        browser.close()
        print('PASS rejected execution and data cycles, unchanged undo history, duplicate connection and valid replacement')
