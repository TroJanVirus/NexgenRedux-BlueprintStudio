from pathlib import Path
from playwright.sync_api import sync_playwright
import os, subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html'
    subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage'])
        page=browser.new_page(viewport={'width':1700,'height':1000});page.set_default_timeout(5000);errors=[]
        page.on('pageerror',lambda e:errors.append(str(e)))
        page.set_content(html.read_text())
        before=page.evaluate('B.compile(doc).source')
        page.get_by_role('button',name='Tools',exact=True).click();page.get_by_role('button',name='Arrange whole graph',exact=True).click()
        assert page.evaluate('B.compile(doc).source')==before
        page.get_by_role('button',name='Find',exact=True).click();page.get_by_placeholder('Find functions, nodes, variables or values…').fill('AddScore')
        page.locator('.workspaceResults button[title=function]').filter(has_text='AddScore  /  AddScore').click()
        assert page.evaluate('graphId')=='AddScore'
        page.locator('.workspaceTabs button').filter(has_text='onEnter').click();assert page.evaluate('graphId')=='onEnter'
        page.locator('#node-name .nodeheader').click();page.get_by_role('button',name='Hide unused data pins',exact=True).click()
        assert page.locator('#node-name').evaluate('(e)=>e.classList.contains("workspaceCollapsed")')
        assert page.evaluate('B.compile(doc).source')==before
        page.locator('#node-spawn .nodeheader').click();page.locator('#node-name .nodeheader').click(modifiers=['Shift'])
        page.get_by_role('button',name='Tools',exact=True).click();page.get_by_role('button',name='Copy selected nodes',exact=True).click()
        count=page.evaluate('graph().nodes.length');page.get_by_role('button',name='Tools',exact=True).click();page.get_by_role('button',name='Paste nodes',exact=True).click()
        assert page.evaluate('graph().nodes.length')==count+2
        page.locator('#undo').click();assert page.evaluate('B.compile(doc).source')==before
        old=page.evaluate('view.x');page.locator('.workspaceMap').click(position={'x':10,'y':10});assert page.evaluate('view.x')!=old
        page.get_by_role('button',name='Tools',exact=True).click();page.get_by_role('button',name='Toggle minimap',exact=True).click();assert page.locator('.workspaceMap').is_hidden()
        page.get_by_role('button',name='Tools',exact=True).click();page.get_by_role('button',name='Toggle minimap',exact=True).click();assert page.locator('.workspaceMap').is_visible()
        handle=page.get_by_role('separator',name='Resize left panel');handle.focus();old=page.locator('main>.left').evaluate('(e)=>e.offsetWidth');handle.press('ArrowRight');assert page.locator('main>.left').evaluate('(e)=>e.offsetWidth')==old+10
        assert page.evaluate('B.compile(doc).source')==before
        assert not errors,errors
        print('PASS automatic arrangement, search, tabs, compact nodes, copy/paste, undo and minimap')
        if os.environ.get('BLUEPRINT_PREVIEW'):page.screenshot(path=os.environ['BLUEPRINT_PREVIEW'])
        browser.close()
