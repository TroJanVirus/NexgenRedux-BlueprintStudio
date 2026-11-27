"""Import a folder using the real browser file input, review, save and reopen."""
from pathlib import Path
from playwright.sync_api import sync_playwright
import json, os, subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp); html=tmp/'studio.html'
    subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    media=tmp/'game'/'media'; demo=media/'showcase'/'demo'; raw=media/'showcase'/'complex'; glob=media/'global'
    for d in (demo/'asset',raw,glob): d.mkdir(parents=True)
    (demo/'main.as').write_text('class Demo : Screen { void onEnter() { Save::set("x", "yes"); } }')
    (demo/'screen.json').write_text('{"title":"Demo","category":"Original"}')
    (demo/'asset'/'a.bin').write_bytes(bytes([0,255]))
    (raw/'main.as').write_text('#include "global:helper.as"\nclass Complex : Screen { void onEnter() { helper(); } }')
    (raw/'screen.json').write_text('{"title":"Complex"}')
    (glob/'helper.as').write_text('void helper() {}')
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage'])
        page=browser.new_page(viewport={'width':1600,'height':1000},accept_downloads=True);page.set_default_timeout(10000)
        errors=[];page.on('pageerror',lambda e:errors.append(str(e)));page.on('dialog',lambda d:d.accept())
        page.set_content(html.read_text())
        page.get_by_role('button',name='File commands').click();page.locator('#importFolder').click()
        page.locator('#folderImportFiles').set_input_files(str(tmp/'game'))
        page.get_by_label('Convert demo').wait_for();assert page.get_by_label('Convert complex').is_disabled()
        assert page.locator('#folderImportApply').is_disabled();page.get_by_label('Convert demo').check();page.locator('#folderImportApply').click()
        assert page.evaluate('project.screens.length===1 && project.nativeScreens.length===1 && project.nativeGlobal.assets.length===1 && last.ok')
        with page.expect_download() as downloaded: page.locator('#save').click()
        saved=tmp/'saved.bp.project.json';saved.write_bytes(downloaded.value.path().read_bytes());p=json.loads(saved.read_text());assert p['nativeScreens'][0]['id']=='complex'
        page.evaluate('document.querySelector("#projectFile").click=()=>{}');page.locator('#projectFile').set_input_files(str(saved));page.wait_for_function('!projectDirty && project.nativeScreens.length===1')
        page.get_by_role('button',name='Game commands').click();page.locator('#projectManager').click();assert 'ORIGINAL SCREENS' in page.locator('#projectDialog').inner_text()
        with page.expect_download() as export: page.get_by_role('button',name='Export original files').click()
        import zipfile
        with zipfile.ZipFile(export.value.path()) as z: assert z.read('main.as')==(raw/'main.as').read_bytes()
        assert not errors,errors;browser.close()
print('PASS real folder selection, opt-in conversion, save/reopen, original screen export')
