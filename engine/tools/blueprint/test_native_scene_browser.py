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
        # The test environment cannot guarantee pointer lock in an embedded browser.
        # Test the pointer-capture fallback with real mouse and keyboard events.
        page.evaluate('HTMLCanvasElement.prototype.requestPointerLock=undefined')
        page.locator('#sceneEditorButton').click();d=page.locator('#nativeSceneEditor');d.get_by_label('Scene type',exact=True).select_option('3D');assert d.get_by_label('View',exact=True).input_value()=='Perspective'
        d.get_by_label('New entity type').select_option('box');d.get_by_role('button',name='+ Entity',exact=True).click();c=d.locator('#sceneCanvas');r=c.bounding_box();x=r['x']+r['width']/2;y=r['y']+r['height']/2
        camera=lambda:json.loads(c.get_attribute('data-camera'))
        original=camera();page.mouse.move(x,y);page.mouse.down(button='right');page.keyboard.down('w');page.wait_for_timeout(250);page.keyboard.up('w');after_w=camera();assert after_w['eye']!=original['eye'];page.keyboard.down('e');page.wait_for_timeout(150);page.keyboard.up('e');after_e=camera();assert after_e['eye'][1]>after_w['eye'][1];page.mouse.move(x+40,y+20,steps=5);after_look=camera();assert after_look['yaw']!=after_e['yaw'] and after_look['pitch']!=after_e['pitch'];page.mouse.up(button='right');page.keyboard.down('w');page.wait_for_timeout(150);page.keyboard.up('w');assert camera()==after_look
        # No object was moved by camera movement.
        assert d.get_by_label('Position X',exact=True).input_value()=='0';assert d.get_by_label('Position Y',exact=True).input_value()=='0';assert d.get_by_label('Position Z',exact=True).input_value()=='0';assert d.get_by_role('button',name='Redo scene edit',exact=True).is_disabled()
        d.get_by_role('button',name='Fit scene',exact=True).click();d.get_by_label('Camera speed').select_option('32');assert d.get_by_label('Camera speed').input_value()=='32'
        if os.environ.get('SCENE_SCREENSHOT'):page.screenshot(path=os.environ['SCENE_SCREENSHOT'])
        # The native editor workflow uses the same local bridge as play testing. Mock HTTP here;
        # the separate JS test covers an authenticated server and a real test child process.
        page.evaluate("""()=>{window.NEXGEN_DEBUG_TOKEN='test';window.nativeRequests=[];window.fetch=async(url,opts)=>{const b=JSON.parse(opts.body);nativeRequests.push(b);let value={};if(b.action==='sceneStart')value={ticket:'native-test'};if(b.action==='sceneRead')value={changed:true,scene:{ground:1,roads:[],entities:[{name:'NativeTree',model:'global:model/tree.glb',place:[1,2,3]}]}};return {ok:true,json:async()=>value};};}""")
        d.get_by_role('button',name='Open in Nexgen',exact=True).click();handoff=page.get_by_role('dialog').filter(has_text='Launch native editor');handoff.get_by_role('button',name='Launch native editor').click();handoff.get_by_role('button',name='Import saved scene').click();handoff.wait_for(state='hidden');d.get_by_role('button',name='NativeTree',exact=True).wait_for();assert page.evaluate('nativeRequests.map(x=>x.action)')==['sceneStart','sceneRead'];assert d.get_by_role('button',name='Undo scene edit').is_enabled();d.get_by_role('button',name='Undo scene edit').click();assert d.get_by_role('button',name='NativeTree',exact=True).count()==0;d.get_by_role('button',name='Redo scene edit').click();d.get_by_role('button',name='NativeTree',exact=True).wait_for();d.get_by_role('button',name='Save to project',exact=True).click();assert page.evaluate('NexgenSceneEditor.decode(project.screens[0].assets.find(a=>a.path==="scene.json").data).ground')==1;d.get_by_role('button',name='Save + connect On Enter',exact=True).click();d.wait_for(state='hidden');assert page.evaluate('B.compile(doc).ok');assert page.evaluate('graph().nodes.some(n=>n.kind==="levelLoad")');assert page.evaluate('B.compile(doc).source.includes("global:scene.as")')
        assert not alerts,alerts;assert not errors,errors;print('PASS real camera input, release, scene unchanged, native editor handoff UI, import undo and save');browser.close()
