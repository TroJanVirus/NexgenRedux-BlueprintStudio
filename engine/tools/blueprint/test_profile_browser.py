from pathlib import Path
from playwright.sync_api import sync_playwright
import os, subprocess, tempfile

root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html'
    subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage'])
        page=browser.new_page(viewport={'width':1700,'height':1000},accept_downloads=True)
        errors=[];page.on('pageerror',lambda e:errors.append(str(e)));page.set_default_timeout(5000)
        page.set_content(html.read_text());page.evaluate("NexgenCommandBar.setExpanded(true)");page.evaluate('''()=>{window.NEXGEN_DEBUG_TOKEN='test';window.profileRequests=[];window.fetch=async(url,opts)=>{const b=JSON.parse(opts.body);profileRequests.push(b);let value={};if(b.action==='events')value={connected:true,events:[]};if(b.cmd==='profile'&&!b.args.on)value={samples:20,seconds:1.2,functions:[{name:'void '+doc.name+'::AddScore(int)',file:'showcase/arcade/main.as',line:8,self:10,total:14}],lines:[{file:'showcase/arcade/main.as',line:10,self:10}]};return {ok:true,json:async()=>value};};}''')
        page.locator('#debugTabButton').click();panel=page.locator('#debugPanel');panel.get_by_role('button',name='Attach for profiling',exact=True).click();page.locator('#dialogBody input').fill('showcase/arcade/main.as');page.locator('#dialogOK').click()
        page.wait_for_function('debugSession?.profileOnly===true && !document.querySelector("#debugConnect").matches(":enabled")')
        assert page.evaluate('debugSession.source===B.compile(doc).source');assert page.evaluate('debugSession.map.length')==0
        panel.get_by_role('button',name='Start profile',exact=True).click();panel.get_by_role('button',name='Stop profile',exact=True).click();panel.get_by_text('20 samples over 1.20 s.',exact=False).wait_for();assert page.evaluate('profileRequests.filter(x=>x.cmd==="profile").map(x=>x.args.on)')==[True,False]
        with page.expect_download() as info:panel.get_by_role('button',name='Save profile',exact=True).click()
        assert info.value.suggested_filename=='nexgen-profile.json'
        page.locator('#debugDetach').click();page.wait_for_function('!debugSession');assert not errors,errors
        print('PASS native profiler UI, ordinary script attach, results and export (mock bridge)');browser.close()
