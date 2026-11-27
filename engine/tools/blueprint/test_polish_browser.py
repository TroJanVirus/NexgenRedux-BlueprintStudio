from pathlib import Path
from playwright.sync_api import sync_playwright
import os, subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html';subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage']);context=browser.new_context(viewport={'width':1700,'height':1000},accept_downloads=True);page=context.new_page();page.set_default_timeout(6000);errors=[]
        page.on('pageerror',lambda e:errors.append(str(e)));page.on('dialog',lambda d:d.accept())
        mock=os.environ.get('BLUEPRINT_MOCK_STORAGE')=='1'
        # The normal test uses real browser storage. A mock can exercise the UI on restricted test hosts.
        if mock:
            page.set_content(html.read_text());page.evaluate("""() => { const rows=new Map(); Object.defineProperty(window,'indexedDB',{configurable:true,value:{open(){const req={};queueMicrotask(()=>{req.result={transaction(){const tx={objectStore(){return {put(v){rows.set(v.id,structuredClone(v));return {result:v.id}},getAll(){return {result:[...rows.values()].map(v=>structuredClone(v))}},delete(k){rows.delete(k);return {}}}}};queueMicrotask(()=>tx.oncomplete());return tx;}};req.onsuccess();});return req;}}}); }""")
        else:
            page.route('http://127.0.0.1:8765/',lambda route:route.fulfill(status=200,content_type='text/html',body=html.read_text()));page.goto('http://127.0.0.1:8765/')
        before=page.evaluate('B.compile(doc).source')
        page.evaluate('selected={link:graph().links[0]};renderDetails()');page.get_by_role('button',name='Add bend',exact=True).click();assert page.locator('.routeHandle').count()==1
        handle=page.locator('.routeHandle').bounding_box();old=page.evaluate('JSON.stringify(graph().links[0].route)');page.mouse.move(handle['x']+handle['width']/2,handle['y']+handle['height']/2);page.mouse.down();page.mouse.move(handle['x']+25,handle['y']+60,steps=4);page.mouse.up();assert page.evaluate('JSON.stringify(graph().links[0].route)')!=old;assert page.evaluate('B.compile(doc).source')==before
        page.get_by_role('button',name='Reset wire route',exact=True).click();assert page.locator('.routeHandle').count()==0
        page.locator('#variables .item').filter(has_text='score').click();page.get_by_role('button',name='Rename variable',exact=True).click();page.locator('#dialogBody input').fill('points');page.locator('#dialogOK').click();assert page.evaluate('B.compile(doc).ok');assert page.evaluate('doc.variables.some(v=>v.name==="points")')
        page.locator('#functions .item').filter(has_text='AddScore').click();page.locator('.node.entry .nodeheader').click();page.get_by_role('button',name='Rename function',exact=True).click();page.locator('#dialogBody input').fill('AwardPoints');page.locator('#dialogOK').click();assert page.evaluate('graphId')=='AwardPoints';assert page.evaluate('B.compile(doc).ok')
        page.locator('.node.entry .nodeheader').click();page.get_by_role('button',name='Bookmark node',exact=True).click();assert page.locator('.bookmarkFlag').count()==1
        page.locator('#events .item').filter(has_text='onEnter').click();page.get_by_role('button',name='Bookmarks',exact=True).click();page.locator('#bookmarkDialog button').filter(has_text='AwardPoints').click();assert page.evaluate('graphId')=='AwardPoints'
        page.locator('#search').fill('Branch');star=page.locator('#palette .favouriteStar').first;star.click();assert page.locator('#palette .category').first.inner_text().lower()=='favourites'
        page.evaluate('createProject(); project.screens[0].assets.push({path:"asset.json",data:"e30="}); projectDirty=true; render()');assert page.locator('#save').inner_text()=='Save project'
        page.wait_for_function("document.querySelector('.toolbar button[title=\"Local recovery copy saved\"]') !== null")
        page.wait_for_timeout(2300)
        if mock:page.evaluate('doc=clone(NexgenExample);project=null;projectActive=null;dirty=false;projectDirty=false;selectGraph(doc.graphs[0].name)')
        else:page.reload()
        page.get_by_role('button',name='Recovery',exact=False).click();page.locator('#recoveryDialog .recoveryRow').first.get_by_role('button',name='Restore',exact=True).click();assert page.evaluate('doc.variables.some(v=>v.name==="points")');assert page.evaluate('project.screens[0].assets[0].path')=='asset.json';assert page.evaluate('graphId')=='AwardPoints';assert page.evaluate('B.compile(doc).ok')
        assert page.locator('#palette .favouriteStar').count()>0
        with page.expect_download() as download:page.locator('#save').click()
        assert download.value.suggested_filename.endswith('.bp.project.json');assert page.evaluate('!dirty&&!projectDirty')
        assert not errors,errors
        print('PASS rename, bookmark navigation, favourites, recovery with assets, and project save'+(' (mock storage; no reload)' if mock else ' (real storage and reload)'))
        browser.close()
