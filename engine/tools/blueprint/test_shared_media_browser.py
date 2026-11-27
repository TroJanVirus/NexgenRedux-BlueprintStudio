from pathlib import Path
from playwright.sync_api import sync_playwright
import json, os, subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html'; subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage']); page=browser.new_page(viewport={'width':1800,'height':1000},accept_downloads=True);page.set_default_timeout(7000);errors=[]
        page.on('pageerror',lambda e:errors.append(str(e)));page.on('dialog',lambda d:d.accept())
        page.set_content(html.read_text());page.evaluate('createProject();project.name="Demo";project.screens.push({id:"two",title:"Level Two",blueprint:clone(doc),assets:[]});render()')
        page.get_by_role('button',name='Assets',exact=True).click();a=page.locator('#assetBrowser');a.get_by_role('button',name='Shared project files',exact=True).click();a.get_by_placeholder('Optional destination folder (e.g. textures)').fill('data')
        with page.expect_file_chooser() as fc:a.get_by_role('button',name='Import files',exact=True).click()
        fc.value.set_files({'name':'message.txt','mimeType':'text/plain','buffer':b'Shared message'});a.get_by_role('button',name='data/message.txt',exact=True).wait_for();assert a.locator('pre').inner_text()=='Shared message'
        assert page.evaluate('project.sharedMedia.namespace')=='demo';assert page.evaluate('project.screens.every(s=>!s.assets.length)');assert page.evaluate('P.assetStats(project).count')==1
        if os.environ.get('MEDIA_SCREENSHOT'):page.screenshot(path=os.environ['MEDIA_SCREENSHOT'])
        a.get_by_role('button',name='Close',exact=True).click();page.evaluate('switchScreen("two");selected={node:"name"};renderDetails()')
        page.locator('#details summary').filter(has_text='Use a project asset').click();page.get_by_role('button',name='Choose file for value',exact=True).click();a.get_by_role('button',name='Shared project files',exact=True).click();a.get_by_role('button',name='data/message.txt',exact=True).click();a.get_by_role('button',name='Use this file',exact=True).click();assert page.evaluate('graph().nodes.find(n=>n.id==="name").defaults.value')=='global:blueprint_demo/data/message.txt'
        with page.expect_download() as dl:page.locator('#save').click()
        saved=Path(dl.value.path()).read_bytes();assert json.loads(saved)['sharedMedia']['assets'][0]['path']=='data/message.txt'
        page.locator('#projectFile').set_input_files({'name':'demo.bp.project.json','mimeType':'application/json','buffer':saved});page.wait_for_function('projectActive!=="two"');assert page.evaluate('project.sharedMedia.assets.length')==1
        page.get_by_role('button',name='Assets',exact=True).click();a.get_by_role('button',name='Shared project files',exact=True).click();a.get_by_role('button',name='data/message.txt',exact=True).click();a.get_by_role('button',name='Remove',exact=True).click();assert page.evaluate('project.sharedMedia.assets.length')==0
        a.get_by_role('button',name='Close',exact=True).click();assert not errors,errors
        print('PASS shared file import, preview, cross-screen picker, project save/reopen and removal');browser.close()
