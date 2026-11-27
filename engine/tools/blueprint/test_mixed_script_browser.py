"""Mixed import, body editing and protection of the original source."""
from pathlib import Path
from playwright.sync_api import sync_playwright
import os, subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html'
    subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage'])
        page=browser.new_page(viewport={'width':1600,'height':1000},accept_downloads=True)
        page.set_default_timeout(5000)
        errors=[];page.on('pageerror',lambda e:errors.append(str(e)));page.on('dialog',lambda d:d.accept())
        page.set_content(html.read_text())
        page.get_by_role('button',name='File commands').click();page.locator('#importScript').click()
        source='class MixedGame : Screen { int score = 0; void onEnter() { Work(); } void Work() { for(int i=0;i<3;i++) { score++; } } }'
        page.locator('#scriptImportSource').fill(source)
        page.locator('#scriptImportMixed').check();page.locator('#scriptImportReview').click()
        assert '1 graph, 1 handwritten function' in page.locator('#scriptImportStatus').inner_text()
        page.locator('#scriptImportApply').click();assert page.evaluate('last.ok')
        assert page.evaluate('doc.graphs[0].nodes.some(n=>n.kind==="call"&&n.ref==="Work")')
        page.evaluate('selectGraph("Work")');assert page.locator('#handwrittenPanel').is_visible()
        original=page.evaluate('graph().handwritten.body');assert 'score++' in original
        assert page.evaluate('B.palette(doc,graph()).length')==0
        page.evaluate('addNode({kind:"print"})');assert page.evaluate('graph().nodes.length')==1
        page.locator('#handwrittenPanel').get_by_role('button',name='Edit function body').click()
        page.locator('#handwrittenBody').fill('} void Extra() {');page.get_by_role('button',name='Apply body').click()
        assert page.locator('#handwrittenDialog').is_visible();assert page.evaluate('graph().handwritten.body')==original
        page.locator('#handwrittenBody').fill('score += 9;');page.get_by_role('button',name='Apply body').click()
        assert not page.locator('#handwrittenDialog').is_visible();assert page.evaluate('graph().handwritten.body')=='score += 9;'
        assert page.evaluate('doc.importedScript.source')==source
        page.locator('#undo').click();assert page.evaluate('graph().handwritten.body')==original
        page.locator('#redo').click();assert page.evaluate('graph().handwritten.body')=='score += 9;'
        # Refuse a still-unsupported body and leave the history untouched.
        page.locator('#undo').click()
        history_len=page.evaluate('history.length')
        page.locator('#handwrittenPanel').get_by_role('button',name='Review conversion to graph').click()
        assert 'no changes made' in page.locator('#functionConversionStatus').inner_text()
        page.locator('#functionConversionDialog').get_by_role('button',name='Close').click()
        assert page.evaluate('history.length')==history_len
        page.locator('#redo').click()
        page.locator('#handwrittenPanel').get_by_role('button',name='Review conversion to graph').click()
        assert 'nodes' in page.locator('#functionConversionStatus').inner_text()
        page.locator('#functionConversionApply').click()
        assert not page.locator('#handwrittenPanel').is_visible()
        assert page.evaluate('graph().convertedScript.body')=='score += 9;'
        assert page.evaluate('graph().nodes.some(n=>n.kind==="set")')
        assert page.evaluate('doc.importedScript.source')==source
        with page.expect_download() as download:
            page.locator('#save').click()
        import json
        converted=json.loads(download.value.path().read_text())
        assert converted['graphs'][1]['convertedScript']['body']=='score += 9;'
        page.locator('#undo').click();assert page.locator('#handwrittenPanel').is_visible()
        assert page.evaluate('graph().handwritten.body')=='score += 9;'
        page.locator('#redo').click();assert not page.locator('#handwrittenPanel').is_visible()
        page.locator('#undo').click()
        page.locator('#deleteGraph').click();assert page.evaluate('doc.graphs.length')==2
        page.evaluate('selectGraph("onEnter")');assert not page.locator('#handwrittenPanel').is_visible()
        assert page.evaluate('document.querySelector("#world").style.visibility')==''
        with page.expect_download() as download:
            page.locator('#save').click()
        import json
        saved=json.loads(download.value.path().read_text());assert saved['importedScript']['source']==source;assert saved['graphs'][1]['handwritten']['body']=='score += 9;'
        assert not errors,errors
        browser.close();print('PASS mixed import, script edit, boundary refusal, undo/redo, protected graph, source backup and saved body')
