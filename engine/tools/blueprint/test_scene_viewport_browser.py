from pathlib import Path
from playwright.sync_api import sync_playwright
import os, subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html'; subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage']);page=browser.new_page(viewport={'width':1800,'height':1050},accept_downloads=True);page.set_default_timeout(5000);errors=[];alerts=[]
        page.on('pageerror',lambda e:errors.append(str(e)))
        def dialog(d):
            if d.type=='alert':alerts.append(d.message)
            d.accept()
        page.on('dialog',dialog);page.set_content(html.read_text());page.locator('#sceneEditorButton').click();d=page.locator('#nativeSceneEditor')
        def change(label,value):
            f=d.get_by_label(label,exact=True);f.fill(value);f.press('Tab')
        def add(kind):
            d.get_by_label('New entity type').select_option(kind);d.get_by_role('button',name='+ Entity',exact=True).click()
        d.get_by_label('Scene type',exact=True).select_option('3D');add('box');change('Name','Player');change('Size X','3');change('Position Y','2');change('Rotation (degrees) Y','45')
        d.get_by_label('View',exact=True).select_option('Top');d.get_by_role('button',name='Fit scene',exact=True).click();d.get_by_label('Snap',exact=True).select_option('1');c=d.locator('#sceneCanvas');r=c.bounding_box();x=r['x']+r['width']/2;y=r['y']+r['height']/2;page.mouse.move(x,y);page.mouse.down();page.mouse.move(x+70,y+20,steps=6);page.mouse.up();assert float(d.get_by_label('Position X',exact=True).input_value())!=0;assert float(d.get_by_label('Position Y',exact=True).input_value())==2
        d.get_by_role('button',name='Undo scene edit',exact=True).click();assert float(d.get_by_label('Position X',exact=True).input_value())==0;d.get_by_role('button',name='Redo scene edit',exact=True).click();assert float(d.get_by_label('Position X',exact=True).input_value())!=0
        d.get_by_label('View',exact=True).select_option('Orbit');add('Light');change('Name','Lamp');change('Light range','12');change('Position X','-3');add('plane');change('Name','Floor');change('Size X','12');change('Size Z','12');change('Position Y','0');d.get_by_role('button',name='Fit scene',exact=True).click()
        if os.environ.get('SCENE_SCREENSHOT'):page.screenshot(path=os.environ['SCENE_SCREENSHOT'])
        d.get_by_label('Scene file',exact=True).fill('levels/demo.json');d.get_by_role('button',name='Save + connect On Enter',exact=True).click();d.wait_for(state='hidden');assert page.evaluate('B.compile(doc).ok');assert page.evaluate('graph().nodes.filter(n=>n.kind.startsWith("api:Space:Space::load:")).length')==1
        data=page.evaluate('NexgenSceneEditor.decode(project.screens[0].assets.find(a=>a.path==="levels/demo.json").data)');assert data['entities'][0]['name']=='Player';assert data['entities'][0]['angles']==[0,45,0];assert data['entities'][1]['range']==12;assert data['entities'][2]['shape']=='plane'
        page.locator('#undo').click();assert page.evaluate('!project.screens[0].assets.some(a=>a.path==="levels/demo.json")');page.locator('#redo').click();assert page.evaluate('project.screens[0].assets.some(a=>a.path==="levels/demo.json")');page.locator('#sceneEditorButton').click();d.get_by_label('Open scene',exact=True).select_option('levels/demo.json');d.get_by_role('button',name='Player',exact=True).click();assert d.get_by_label('Rotation (degrees) Y',exact=True).input_value()=='45';d.get_by_role('button',name='Save + connect On Enter',exact=True).click();d.wait_for(state='hidden');assert page.evaluate('graph().nodes.filter(n=>n.kind.startsWith("api:Space:Space::load:")).length')==1
        assert not alerts,alerts;assert not errors,errors;print('PASS scene placement, snapping, undo/redo, inspector, scene save, automatic load wiring and reopening');browser.close()
