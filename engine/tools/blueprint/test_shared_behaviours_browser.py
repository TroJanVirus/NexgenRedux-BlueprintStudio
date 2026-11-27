from pathlib import Path
from playwright.sync_api import sync_playwright
import json, os, subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html'; subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage']); page=browser.new_page(viewport={'width':1800,'height':1000},accept_downloads=True); page.set_default_timeout(7000); errors=[]
        page.on('pageerror',lambda e:errors.append(str(e))); page.on('dialog',lambda d:d.accept())
        page.set_content(html.read_text()); screen=json.loads((root/'examples/messages.bp.json').read_text());page.evaluate('(s)=>{doc=s;selectGraph(doc.graphs[0].name);createProject();project.screens.push({id:"two",title:"Level Two",blueprint:clone(s),assets:[]});render()}',screen)
        name=screen['components'][0]['name']
        page.locator('#components .item').first.click();page.get_by_role('button',name='Share in project',exact=True).click();assert page.evaluate('project.sharedBehaviours.length')==1;assert page.evaluate('doc.sharedComponents[0]')==name
        page.locator('#undo').click();assert page.evaluate('!doc.sharedComponents&&project.sharedBehaviours.length===0');page.locator('#redo').click();assert page.evaluate('doc.sharedComponents.length===1&&project.sharedBehaviours.length===1')
        page.evaluate('switchScreen("two")');page.locator('#sharedBehavioursButton').click();dlg=page.locator('#sharedBehaviours');dlg.get_by_role('button',name='Link to screen',exact=True).click();assert page.evaluate('Shared.uses(project,doc.sharedComponents[0]).length')==2
        if os.environ.get('SHARED_SCREENSHOT'):page.screenshot(path=os.environ['SHARED_SCREENSHOT'])
        dlg.get_by_role('button',name='Edit',exact=True).click();assert page.evaluate('doc.kind')=='Behaviour'
        page.evaluate('remember();doc.variables[0].default=17;render()');page.locator('#backToScreen').click();assert page.evaluate('project.screens.every(s=>s.blueprint.components[0].variables[0].default===17)')
        page.locator('#undo').click();assert page.evaluate('project.screens.every(s=>s.blueprint.components[0].variables[0].default===0)');page.locator('#redo').click();assert page.evaluate('project.screens.every(s=>s.blueprint.components[0].variables[0].default===17)')
        page.locator('#components .item').first.dblclick();page.evaluate('remember();doc.variables[0].default=29;render()')
        with page.expect_download() as dl:page.locator('#save').click()
        saved=json.loads(Path(dl.value.path()).read_text());assert saved['sharedBehaviours'][0]['variables'][0]['default']==29;assert all(s['blueprint']['components'][0]['variables'][0]['default']==29 for s in saved['screens'])
        page.locator('#backToScreen').click();page.locator('#components .item').first.click();page.get_by_role('button',name='Make local copy',exact=True).click();assert page.evaluate('!doc.sharedComponents')
        page.locator('#components .item').first.dblclick();page.evaluate('remember();doc.variables[0].default=40;render()');page.locator('#backToScreen').click();page.evaluate('captureProject()');assert page.evaluate('project.sharedBehaviours[0].variables[0].default')==29
        assert page.evaluate('project.screens.find(s=>s.id==="two").blueprint.components[0].variables[0].default')==40
        page.locator('#projectFile').set_input_files({'name':'shared.bp.project.json','mimeType':'application/json','buffer':json.dumps(saved).encode()});page.wait_for_function('projectActive!=="two"&&doc.kind==="Screen"');assert page.evaluate('doc.components[0].variables[0].default')==29;assert page.evaluate('P.checkProject(project).every(r=>r.ok)')
        assert not errors,errors
        print('PASS sharing, linking, global edit, undo/redo, saving while editing, local copy and project reopen')
        browser.close()
