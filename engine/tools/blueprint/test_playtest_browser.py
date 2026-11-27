from pathlib import Path
from playwright.sync_api import sync_playwright
import os, subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html';subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage']);page=browser.new_page(viewport={'width':1700,'height':1000});page.set_default_timeout(6000);errors=[]
        page.on('pageerror',lambda e:errors.append(str(e)));page.set_content(html.read_text())
        page.get_by_role('button',name='Play test',exact=True).click();d=page.locator('#playtestDialog');assert '--preview-engine' in d.inner_text();d.get_by_role('button',name='Close').click()
        page.evaluate('''()=>{window.NEXGEN_DEBUG_TOKEN='test';window.playCalls=[];let running=false,logs='';window.fetch=async(url,opts)=>{if(url!=='/play')throw Error('Unexpected request');const data=JSON.parse(opts.body);playCalls.push(data);if(data.action==='start'){running=true;logs='Started test';}if(data.action==='reload')logs='Updated script';if(data.action==='stop'){running=false;logs='Stopped test';}return {ok:true,json:async()=>({configured:true,running,screen:'arcade',logs})};};}''')
        page.get_by_role('button',name='Play test',exact=True).click();d.get_by_role('button',name='Run game').click();d.get_by_text('Started test',exact=True).wait_for();assert page.evaluate('playCalls.find(c=>c.action==="start").project.screens.length')==1
        d.get_by_role('button',name='Close').click();page.evaluate('remember();graph().nodes.find(n=>n.id==="announce").defaults.text="Updated while testing";render()');page.get_by_role('button',name='Play test',exact=False).click();d.get_by_role('button',name='Update scripts').click();d.get_by_text('Updated script',exact=True).wait_for();assert page.evaluate('playCalls.find(c=>c.action==="reload").project.screens[0].blueprint.graphs[0].nodes.find(n=>n.id==="announce").defaults.text')=='Updated while testing'
        d.get_by_role('button',name='Stop',exact=True).click();d.get_by_text('Stopped test',exact=True).wait_for();d.get_by_role('button',name='Close').click()
        page.evaluate('''()=>{project.nativeScreens=[{id:'original_game',title:'Original game',assets:[{path:'main.as',data:'YQ=='},{path:'screen.json',data:'e30='}]}];project.nativeGlobal={assets:[{path:'helper.as',data:'YQ=='}]};}''')
        page.get_by_role('button',name='Play test',exact=False).click();d.locator('select').select_option('original_game');d.get_by_role('button',name='Run game').click();d.get_by_text('Started test',exact=True).wait_for();assert page.evaluate('playCalls.filter(c=>c.action==="start").at(-1).screen')=='original_game';assert page.evaluate('playCalls.filter(c=>c.action==="start").at(-1).project.nativeGlobal.assets[0].path')=='helper.as';d.get_by_role('button',name='Stop',exact=True).click();d.get_by_text('Stopped test',exact=True).wait_for();d.get_by_role('button',name='Close').click();assert not errors,errors
        print('PASS standalone setup, play-test controls and unsaved graph update payload (mock HTTP)');browser.close()
