from pathlib import Path
from playwright.sync_api import sync_playwright
import os, subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html';subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage']);page=browser.new_page(viewport={'width':1700,'height':1000},accept_downloads=True);page.set_default_timeout(5000);errors=[]
        page.on('pageerror',lambda e:errors.append(str(e)));page.on('dialog',lambda d:d.accept('Group' if d.type=='prompt' else ''))
        page.set_content(html.read_text());page.evaluate("NexgenCommandBar.setExpanded(true)")
        before=page.evaluate('B.compile(doc).source')
        page.locator('#node-spawn .nodeheader').click();page.locator('#node-name .nodeheader').click(modifiers=['Shift']);page.get_by_role('button',name='Group',exact=True).click()
        page.get_by_role('button',name='Fold',exact=True).click()
        assert page.locator('.foldedGroup').count()==1
        assert not page.locator('#node-spawn').is_visible()
        assert page.locator('.foldedGroup .port').count()>=2
        assert page.evaluate('B.compile(doc).source')==before
        assert page.evaluate('document.querySelectorAll("#wires .wire").length < graph().links.length')
        # Outer group folds the inner group, then restores its folded state on expand.
        page.evaluate("graph().frames.push({title:'Outer',color:'#426a8e',nodes:['spawn','name','rect'],collapsed:true});render()")
        assert page.locator('.foldedGroup').count()==1
        assert page.locator('.foldedGroup').inner_text().startswith('Outer')
        page.get_by_role('button',name='Expand',exact=True).click();assert page.locator('.foldedGroup').inner_text().startswith('Group'),page.locator('.foldedGroup').inner_text()
        page.get_by_role('button',name='Expand',exact=True).click();assert page.locator('#node-spawn').is_visible()
        assert page.evaluate('B.compile(doc).source')==before
        with page.expect_download() as d:page.get_by_role('button',name='Graph image',exact=True).click()
        assert d.value.suggested_filename.endswith('.svg');content=Path(d.value.path()).read_text();assert '<svg ' in content and 'BP_Arcade / onEnter' in content
        page.locator('#functions .item').filter(has_text='AddScore').click();page.locator('.node.entry .nodeheader').click();page.get_by_role('button',name='Edit function inputs',exact=True).click();dlg=page.locator('#signatureEditor');dlg.locator('input').first.fill('points');dlg.get_by_role('button',name='+ Input',exact=True).click();dlg.get_by_role('button',name='Apply',exact=True).click();assert not dlg.is_visible();assert page.evaluate('B.compile(doc).ok');assert page.evaluate('graph().params[0].name')=='points';assert page.evaluate('graph().params.length')==2
        page.get_by_role('button',name='Panels',exact=True).click();page.get_by_role('button',name='Swapped',exact=True).click();assert page.locator('main>.right').bounding_box()['x']<page.locator('main>.left').bounding_box()['x']
        assert not errors,errors
        print('PASS folded groups, nested groups, external pins, SVG download, function input editing and panel arrangement')
        browser.close()
