from pathlib import Path
from playwright.sync_api import sync_playwright
import subprocess, os, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html'; subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage'])
        page=browser.new_page(viewport={'width':1700,'height':1000});page.set_default_timeout(5000);errors=[]
        page.on('pageerror',lambda e:errors.append(str(e)))
        page.on('dialog',lambda d:d.accept('Setup' if d.type=='prompt' else ''))
        page.set_content(html.read_text());page.evaluate("NexgenCommandBar.setExpanded(true)");page.wait_for_timeout(100)
        before=page.evaluate('B.compile(doc).source')
        page.locator('#node-spawn .nodeheader').click();page.locator('#node-name .nodeheader').click(modifiers=['Shift'])
        assert page.locator('.node.selected').count()==2
        page.locator('#details button').filter(has_text='Group').click();assert page.locator('.layoutFrame').count()==1
        page.locator('#details button').filter(has_text='Align top').click();assert page.evaluate('graph().nodes.find(n=>n.id==="name").y===graph().nodes.find(n=>n.id==="spawn").y')
        page.locator('.layoutFrame button').first.click();assert page.locator('.node.selected').count()==2
        a=page.locator('#node-spawn .nodeheader').bounding_box();old=page.evaluate('graph().nodes.find(n=>n.id==="spawn").x');page.mouse.move(a['x']+10,a['y']+10);page.mouse.down();page.mouse.move(a['x']+45,a['y']+40,steps=5);page.mouse.up();assert page.evaluate('graph().nodes.find(n=>n.id==="spawn").x')!=old
        page.locator('#node-rect .nodeheader').click();page.locator('#details input').filter(has_text='').count()
        page.locator('#details label').filter(has_text='Display title').locator('xpath=following-sibling::input[1]').fill('Player shape');page.locator('#details label').filter(has_text='Display title').locator('xpath=following-sibling::input[1]').press('Tab');assert 'Player shape' in page.locator('#node-rect').inner_text()
        page.get_by_role('button',name='Layout',exact=True).click();page.locator('#layoutDialog label').filter(has_text='Wire style').locator('xpath=following-sibling::select[1]').select_option('Straight');page.locator('#layoutDialog label').filter(has_text='Show grid').locator('xpath=following-sibling::input[1]').uncheck();page.locator('#layoutDialog').get_by_role('button',name='Close',exact=True).click();assert page.locator('#viewport').evaluate('(e)=>e.style.backgroundImage')=='none';assert ' L ' in page.locator('#wires path').first.get_attribute('d')
        assert page.evaluate('B.compile(doc).source')==before
        page.evaluate('selected=null;renderGraph();renderDetails()');page.locator('#node-spawn .nodeheader').click()
        for n in ['name','rect','store','announce']:page.locator('#node-'+n+' .nodeheader').click(modifiers=['Shift'])
        assert page.locator('.node.selected').count()==5
        page.get_by_role('button',name='Move to function',exact=True).click();assert page.evaluate('doc.graphs.some(g=>g.name==="Setup")');assert page.evaluate('B.compile(doc).ok')
        page.locator('.node').filter(has_text='Call Setup').locator('.nodeheader').dblclick();assert 'Setup' in page.locator('#graphName').inner_text();page.get_by_role('button',name='←',exact=True).click();assert 'onEnter' in page.locator('#graphName').inner_text()
        page.locator('#functions .item').filter(has_text='Setup').click();page.get_by_role('button',name='Layout',exact=True).click();page.locator('#layoutDialog label').filter(has_text='Function / macro folder').locator('xpath=following-sibling::input[1]').fill('Player');page.locator('#layoutDialog label').filter(has_text='Function / macro folder').locator('xpath=following-sibling::input[1]').press('Tab');page.locator('#layoutDialog').get_by_role('button',name='Close',exact=True).click();assert 'Player' in page.locator('#functions').inner_text()
        page.locator('#addFunction').click();fd=page.locator('#functionEditor');fd.locator('input').nth(0).fill('GetHealth');fd.locator('input').nth(1).fill('Player');fd.locator('select').select_option('int');fd.locator('input[type=checkbox]').check();fd.get_by_role('button',name='+ Add input',exact=True).click();fd.locator('.row input').fill('amount');fd.locator('.row select').select_option('int');fd.get_by_role('button',name='Create',exact=True).click();assert page.evaluate('graph().params[0].name==="amount" && graph().pure && graph().returns==="int"');assert page.evaluate('B.compile(doc).ok')
        page.get_by_role('button',name='Layout',exact=True).click();page.locator('#layoutDialog').get_by_role('button',name='Light theme',exact=True).click();assert page.locator('#viewport').evaluate('(e)=>e.style.backgroundColor')=='rgb(248, 250, 252)'
        assert not errors,errors
        print('PASS groups, drag, alignment, custom titles, wires, grid, refactor, navigation and function folders')
        if os.environ.get('BLUEPRINT_PREVIEW'):page.screenshot(path=os.environ['BLUEPRINT_PREVIEW'])
        browser.close()
