"""Check the compact toolbar using the actual controls, not a substitute."""
from pathlib import Path
from playwright.sync_api import sync_playwright
import os, subprocess, tempfile

root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html'
    subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage'])
        page=browser.new_page(viewport={'width':1280,'height':800})
        page.set_default_timeout(4000)
        errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
        page.set_content(html.read_text())
        page.evaluate('NexgenCommandBar.setExpanded(false)')
        assert page.locator('#save').is_visible() and page.locator('#compile').is_visible()
        assert not page.locator('#projectManager').is_visible()
        trigger=page.get_by_role('button',name='Game commands',exact=True)
        trigger.click();assert page.locator('#projectManager').is_visible()
        page.locator('#projectManager').click();assert page.locator('#projectDialog').is_visible()
        page.locator('#projectDialog').evaluate('(e)=>e.close()')
        trigger.focus();trigger.press('ArrowDown');assert page.locator('#projectManager').evaluate('(e)=>e===document.activeElement')
        page.keyboard.press('ArrowDown');assert page.locator('#sceneEditorButton').evaluate('(e)=>e===document.activeElement')
        page.evaluate('selected={node:graph().nodes.find(n=>n.kind!=="entry").id}')
        before=page.evaluate('graph().nodes.length');page.keyboard.press('Delete');assert page.evaluate('graph().nodes.length')==before
        page.keyboard.press('Escape');assert not page.locator('#projectManager').is_visible() and trigger.evaluate('(e)=>e===document.activeElement')
        file=page.get_by_role('button',name='File commands',exact=True)
        file.focus();page.keyboard.press('Space');assert page.locator('#new').is_visible();page.get_by_role('button',name='Show all toolbar commands').click()
        assert page.locator('#projectManager').is_visible() and page.locator('#compactCommands').is_visible()
        page.locator('#compactCommands').click();assert not page.locator('#projectManager').is_visible()
        page.set_viewport_size({'width':900,'height':700})
        trigger.click();box=page.locator('.commandMenu:popover-open').bounding_box();assert box and box['x']>=0 and box['x']+box['width']<=900 and box['y']+box['height']<=700,box
        page.keyboard.press('Escape');assert not page.locator('#projectManager').is_visible()
        trigger.click();trigger.click();assert not page.locator('#projectManager').is_visible()
        assert not errors,errors
        browser.close()
        print('PASS compact toolbar, native commands, keyboard, full toolbar and narrow window')
