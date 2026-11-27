"""Exercise the browser export against a real local bridge and Nexgen Cook."""
import base64, os, socket, struct, subprocess, time, zipfile
from pathlib import Path
from urllib.request import urlopen, Request
from urllib.error import HTTPError
from playwright.sync_api import sync_playwright
root=Path(__file__).resolve().parent
cook=os.environ.get('NEXGEN_COOK')
if not cook:
    print('SKIP real cook browser test: set NEXGEN_COOK')
    raise SystemExit(0)
with socket.socket() as s:
    s.bind(('127.0.0.1',0));port=s.getsockname()[1]
url=f'http://127.0.0.1:{port}'
p=subprocess.Popen(['node',str(root/'debug_bridge.js'),'127.0.0.1','4711',str(port),'--cook',cook],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
try:
    for i in range(100):
        try:
            with urlopen(url,timeout=1) as response: assert response.status==200
            break
        except OSError:
            if p.poll() is not None: raise RuntimeError(p.stderr.read().decode())
            time.sleep(.05)
    else: raise RuntimeError('Bridge did not start')
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage'])
        page=browser.new_page(viewport={'width':1700,'height':1000},accept_downloads=True);page.set_default_timeout(8000);errors=[]
        page.on('pageerror',lambda e:errors.append(str(e)))
        if os.environ.get('BLUEPRINT_FORWARD_HTTP')=='1':
            # Restricted browser hosts can forward the request through Python. The native
            # bridge, authentication and Cook still run; the browser transport is substituted.
            def forward(options):
                req=Request(url+'/cook',data=options['body'].encode(),headers={**options['headers'],'Origin':url},method='POST')
                try: response=urlopen(req,timeout=30)
                except HTTPError as e: response=e
                with response: return {'status':response.status,'headers':dict(response.headers),'data':base64.b64encode(response.read()).decode()}
            page.expose_function('forwardCook',forward)
            with urlopen(url) as response: page.set_content(response.read().decode())
            page.evaluate("""()=>{window.fetch=async (url,opts)=>{if(url!=='/cook')throw Error('Unexpected request');const r=await forwardCook(opts);return new Response(Uint8Array.from(atob(r.data),x=>x.charCodeAt(0)),{status:r.status,headers:r.headers});};}""")
        else: page.goto(url)
        w=h=64;pixels=bytes([100])*(w*h*3);bmp=struct.pack('<2sIHHI',b'BM',54+len(pixels),0,0,54)+struct.pack('<IiiHHIIiiII',40,w,h,1,24,0,len(pixels),0,0,0,0)+pixels
        page.evaluate('data=>{createProject();project.screens[0].assets=[{path:"texture.bmp",data}];}',base64.b64encode(bmp).decode())
        page.get_by_role('button',name='File commands').click();page.locator('#exportProject').click();page.locator('#dialogBody select').select_option('Cooked release (original Xbox, images and sounds)')
        with page.expect_download(timeout=15000) as download:
            page.locator('#dialogOK').click()
        assert download.value.suggested_filename.endswith('-xboxog.zip')
        with zipfile.ZipFile(download.value.path()) as z:
            assert 'COOK.txt' in z.namelist()
            name=next(n for n in z.namelist() if n.endswith('/asset/texture.bmp'))
            assert len(z.read(name))<len(bmp)
            assert any(n.endswith('.bp.json') for n in z.namelist())
        assert page.evaluate('project.screens[0].assets[0].data')==base64.b64encode(bmp).decode()
        assert not errors,errors;browser.close()
        print('PASS browser export, authenticated local bridge, native Cook and downloaded Xbox package'+(' (HTTP forwarded through Python)' if os.environ.get('BLUEPRINT_FORWARD_HTTP')=='1' else ''))
finally:
    p.terminate();p.wait(timeout=10)
