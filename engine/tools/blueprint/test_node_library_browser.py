from pathlib import Path
from playwright.sync_api import sync_playwright
import os, subprocess, tempfile
root=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    html=Path(tmp)/'studio.html'
    subprocess.run(['node',str(root/'bundle.js'),str(html)],check=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage'])
        page=browser.new_page(viewport={'width':1700,'height':1000})
        errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
        page.set_content(html.read_text());page.set_default_timeout(5000)
        first=page.locator('#palette .category').first
        name=first.inner_text();first.click();assert first.get_attribute('aria-expanded')=='false'
        page.locator('#search').fill('camera space');assert page.locator('#palette .category').first.inner_text().lower()=='space'
        page.locator('#search').press('ArrowDown');assert page.locator('#palette .nodeLibraryChoice').count()==1
        page.locator('#search').press('Enter');assert page.evaluate('graph().nodes.some(n=>n.kind.startsWith("api:Space:Space::camera"))')
        assert page.locator('#details .nodeLibraryHelp').get_attribute('href').endswith('#space')
        page.locator('#search').fill('');assert page.locator('#palette .category').filter(has_text=name).first.get_attribute('aria-expanded')=='false'
        page.evaluate('showContext(650,350,{dir:"out",type:"Entity@",node:"spawn",pin:"result"})')
        page.locator('#context input').fill('destroy');assert page.locator('#context .item').count()>0
        page.locator('#context input').press('Escape');assert page.locator('#context').is_hidden()
        assert not errors,errors
        browser.close();print('PASS node search, collapsible categories, keyboard insertion, context filtering and upstream reference link')
