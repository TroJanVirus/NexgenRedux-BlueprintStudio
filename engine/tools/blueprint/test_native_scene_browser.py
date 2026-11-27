from pathlib import Path
from playwright.sync_api import sync_playwright
import os, subprocess, tempfile, json
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html'; subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage']);page=browser.new_page(viewport={'width':1800,'height':1050});page.set_default_timeout(5000);errors=[];alerts=[]
        page.on('pageerror',lambda e:errors.append(str(e)))
        def dialog(d):
            if d.type=='alert':alerts.append(d.message)
            d.accept()
        page.on('dialog',dialog);page.set_content(html.read_text())
        page.locator('#sceneEditorButton').click();d=page.locator('#nativeSceneEditor');assert d.get_by_label('Scene type',exact=True).input_value()=='3D'
        assert d.locator('#sceneCanvas').count()==0
        assert d.get_by_role('button',name='+ Entity',exact=True).count()==0
        assert d.get_by_role('button',name='Open in Nexgen',exact=True).is_visible()
        if os.environ.get('SCENE_SCREENSHOT'):page.screenshot(path=os.environ['SCENE_SCREENSHOT'])
        # A simple scene may already be connected before it is upgraded in the native editor.
        d.get_by_role('button',name='Save + connect On Enter',exact=True).click();d.wait_for(state='hidden')
        old_id=page.evaluate('graph().nodes.find(n=>n.kind==="api:Space:Space::load:string,Vec3,bool").id')
        page.locator('#sceneEditorButton').click();d.get_by_label('Open scene',exact=True).select_option('level.json')
        # The native editor workflow uses the same local bridge as play testing. Mock HTTP here;
        # the separate JS test covers an authenticated server and a real test child process.
        page.evaluate("""()=>{window.NEXGEN_DEBUG_TOKEN='test';window.nativeRequests=[];window.fetch=async(url,opts)=>{const b=JSON.parse(opts.body);nativeRequests.push(b);let value={};if(b.action==='sceneStart')value={ticket:'native-test'};if(b.action==='sceneRead')value={changed:true,scene:{ground:1,roads:[],entities:[{name:'NativeTree',model:'asset:native-import/abc/tree.glb',place:[1,2,3]}]},assets:[{path:'native-import/abc/tree.glb',data:'YQ=='}]};return {ok:true,json:async()=>value};};}""")
        d.get_by_role('button',name='Open in Nexgen',exact=True).click();handoff=page.get_by_role('dialog').filter(has_text='Launch native editor');handoff.get_by_role('button',name='Launch native editor').click();handoff.get_by_role('button',name='Import saved scene').click();handoff.wait_for(state='hidden');d.get_by_role('button',name='NativeTree',exact=True).wait_for();assert page.evaluate('nativeRequests.map(x=>x.action)')==['sceneStart','sceneRead'];assert d.get_by_role('button',name='Undo scene edit').is_enabled();d.get_by_role('button',name='Undo scene edit').click();assert d.get_by_role('button',name='NativeTree',exact=True).count()==0;d.get_by_role('button',name='Redo scene edit').click();d.get_by_role('button',name='NativeTree',exact=True).wait_for();d.get_by_role('button',name='Save to project',exact=True).click();assert page.evaluate('NexgenSceneEditor.decode(project.screens[0].assets.find(a=>a.path==="level.json").data).ground')==1;assert page.evaluate('project.screens[0].assets.some(a=>a.path==="native-import/abc/tree.glb")');d.get_by_role('button',name='Save + connect On Enter',exact=True).click();d.wait_for(state='hidden');assert page.evaluate('B.compile(doc).ok');assert page.evaluate('graph().nodes.find(n=>n.kind==="levelLoad").id')==old_id;assert page.evaluate('graph().nodes.filter(n=>n.kind==="levelLoad").length')==1;assert page.evaluate('B.compile(doc).source.includes("global:scene.as")')
        assert not alerts,alerts;assert not errors,errors;print('PASS native editor first, no duplicate 3D viewport, handoff UI, imported media, undo and save');browser.close()
