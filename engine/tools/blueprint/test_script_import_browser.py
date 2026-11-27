"""Test the real import dialog, failure safety and saved original source."""
from pathlib import Path
from playwright.sync_api import sync_playwright
import os,subprocess,tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html'
    subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage'])
        page=browser.new_page(viewport={'width':1600,'height':1000},accept_downloads=True)
        errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
        page.on('dialog',lambda d:d.accept())
        page.set_content(html.read_text())
        def open_import():
            if not page.locator('#importScript').is_visible():
                page.get_by_role('button',name='File commands').click()
            page.locator('#importScript').click()
        original_doc=page.evaluate('JSON.stringify(doc)')
        open_import()
        page.locator('#scriptImportSource').fill('class Broken : Screen { void onEnter() { while (true) {} } }')
        page.locator('#scriptImportReview').click()
        assert 'no graph was changed' in page.locator('#scriptImportStatus').inner_text()
        assert page.locator('#scriptImportApply').is_disabled()
        assert page.evaluate('JSON.stringify(doc)')==original_doc
        original='\ufeff// Original comment\r\nclass Player : Screen {\r\n int health = 100;\r\n void onEnter() { health = 100; }\r\n void TakeDamage(int damage) { health -= damage; if (health <= 0) { health = 0; } }\r\n}'
        page.locator('#scriptImportFile').set_input_files({'name':'player.as','mimeType':'text/plain','buffer':original.encode('utf8')})
        page.wait_for_function("document.querySelector('#scriptImportStatus').textContent.includes('Source loaded')")
        page.locator('#scriptImportReview').click()
        assert '2 graphs' in page.locator('#scriptImportStatus').inner_text()
        assert page.locator('#scriptImportApply').is_enabled()
        assert page.evaluate('JSON.stringify(doc)')==original_doc
        with page.expect_download() as d:
            page.get_by_role('button',name='Save original source',exact=True).click()
        assert d.value.path().read_bytes()==original.encode('utf8')
        page.locator('#scriptImportApply').click()
        assert page.evaluate('doc.name')=='Player'
        assert page.evaluate('doc.importedScript.source')==original
        assert page.evaluate('last.ok')
        assert page.evaluate('doc.graphs.find(g=>g.name==="TakeDamage").nodes.some(n=>n.kind==="branch")')
        page.evaluate("selectGraph('TakeDamage'); selected={node:graph().nodes.find(n=>n.kind==='branch').id};renderDetails()")
        assert page.get_by_role('button',name='Original script · line 5').is_visible()
        # Exported Blueprint has original, not just converted graph.
        with page.expect_download() as d:
            page.locator('#save').click()
        import json
        assert json.loads(d.value.path().read_text())['importedScript']['source']==original
        # Text changes invalidate pending conversions.
        open_import();page.locator('#scriptImportReview').click()
        page.locator('#scriptImportSource').fill('not a script')
        assert page.locator('#scriptImportApply').is_disabled()
        page.locator('#scriptImportDialog').get_by_role('button',name='Close',exact=True).click()
        assert page.evaluate('doc.name')=='Player'
        assert not errors,errors
        browser.close()
        print('PASS file import, exact BOM/CRLF preservation, reviewed conversion, source reference, save and refusal safety')
