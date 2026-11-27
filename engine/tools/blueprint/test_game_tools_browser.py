from pathlib import Path
from playwright.sync_api import sync_playwright
import base64, json, os, subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html'; subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage']);page=browser.new_page(viewport={'width':1700,'height':1000},accept_downloads=True);page.set_default_timeout(6000);errors=[]
        page.on('pageerror',lambda e:errors.append(str(e)));page.on('dialog',lambda d:d.accept())
        page.set_content(html.read_text());page.evaluate("NexgenCommandBar.setExpanded(true)");screen=json.loads((root/'examples/messages.bp.json').read_text());page.evaluate('(s)=>{doc=s;selectGraph(doc.graphs[0].name);createProject();render()}',screen)
        page.locator('#components .item').first.dblclick();assert page.evaluate('doc.kind')=='Behaviour';assert page.locator('#backToScreen').is_visible();assert page.locator('#save').inner_text()=='Save project'
        page.locator('#variables .item').first.click();page.evaluate('remember();doc.variables[0].default=17;render()');assert page.evaluate('rootDocument().components[0].variables[0].default')==17
        with page.expect_download() as info:page.locator('#save').click()
        saved=json.loads(Path(info.value.path()).read_text());assert saved['screens'][0]['blueprint']['components'][0]['variables'][0]['default']==17
        page.locator('#backToScreen').click();assert page.evaluate('doc.kind')=='Screen';assert page.evaluate('doc.components[0].variables[0].default')==17;assert not page.evaluate('dirty');assert page.evaluate('B.compile(doc).ok')
        page.locator('#undo').click();assert page.evaluate('doc.components[0].variables[0].default')==0;page.locator('#redo').click();assert page.evaluate('doc.components[0].variables[0].default')==17
        page.get_by_role('button',name='Assets',exact=True).click();a=page.locator('#assetBrowser');a.get_by_placeholder('Optional destination folder (e.g. textures)').fill('data')
        with page.expect_file_chooser() as chooser:a.get_by_role('button',name='Import files',exact=True).click()
        chooser.value.set_files({'name':'test.json','mimeType':'application/json','buffer':b'{"value":7}'});a.get_by_role('button',name='data/test.json',exact=True).wait_for();assert a.locator('pre').inner_text()=='{"value":7}'
        with page.expect_file_chooser() as chooser:a.get_by_role('button',name='Import files',exact=True).click()
        chooser.value.set_files({'name':'dot.png','mimeType':'image/png','buffer':base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jpXcAAAAASUVORK5CYII=')});a.locator('img').wait_for();a.locator('img').evaluate('(e)=>e.decode()');assert a.locator('img').evaluate('(e)=>e.naturalWidth===1')
        a.get_by_role('button',name='Close',exact=True).click();page.evaluate('project.screens.push({id:"draft",title:"Draft",blueprint:clone(NexgenExample),assets:[]});project.screens[1].blueprint.graphs[0].nodes[1].kind="missing";projectDirty=true')
        page.get_by_role('button',name='Check project',exact=True).click();assert '1 / 2 screens compile' in page.locator('#projectChecks').inner_text();page.locator('#projectChecks .projectScreen').filter(has_text='Draft').get_by_role('button',name='Open').click();assert page.evaluate('projectActive')=='draft';assert not page.evaluate('B.compile(doc).ok')
        page.evaluate('project.screens[1].assets=[{path:"sample.json",data:"e30="}];selected={node:"name"};renderDetails()');page.locator('#details summary').filter(has_text='Use a project asset').click();page.get_by_role('button',name='Choose file for value',exact=True).click();a.get_by_role('button',name='sample.json',exact=True).click();a.get_by_role('button',name='Use this file').click();assert page.evaluate('graph().nodes.find(n=>n.id==="name").defaults.value')=='asset:sample.json'
        with page.expect_download() as info:page.locator('#save').click()
        saved_draft=Path(info.value.path()).read_bytes();page.locator('#projectFile').set_input_files({'name':'unfinished.bp.project.json','mimeType':'application/json','buffer':saved_draft});page.wait_for_function('projectActive==="messages" || projectActive==="bp_messages"');assert page.evaluate('project.screens.some(s=>s.id==="draft"&&!B.compile(s.blueprint).ok)')
        assert not errors,errors
        print('PASS direct behaviour editing, project save/undo, asset import and preview, asset picker, project checks and unfinished-project reopening')
        browser.close()
