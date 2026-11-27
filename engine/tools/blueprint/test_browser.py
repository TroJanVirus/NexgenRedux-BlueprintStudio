from playwright.sync_api import sync_playwright
from pathlib import Path
import zipfile,json,os,subprocess,tempfile
root=Path(__file__).resolve().parent
# Optional developer test: python -m pip install playwright; playwright install chromium
work=Path(tempfile.mkdtemp(prefix='nexgen-blueprint-browser-'))
bundle=work/'studio.html'
subprocess.run(['node',str(root/'bundle.js'),str(bundle)],check=True)
with sync_playwright() as pw:
    browser=pw.chromium.launch(executable_path=os.environ.get('CHROMIUM_PATH'),args=['--no-sandbox','--disable-dev-shm-usage'])
    page=browser.new_page(viewport={'width':1600,'height':1000},accept_downloads=True)
    page.set_default_timeout(5000)
    errors=[];page.on('pageerror',lambda e:errors.append(str(e)));page.on('dialog',lambda d:d.accept())
    page.set_content(bundle.read_text());page.wait_for_timeout(150)
    assert page.locator('#status').inner_text().startswith('Compiled')
    page.locator('#examplesButton').click();page.locator('#dialogBody select').select_option('Extended');page.locator('#dialogOK').click();page.wait_for_timeout(120)
    assert page.locator('#status').inner_text().startswith('Compiled'); assert 'DoubleValue' in page.locator('#macros').inner_text()
    page.locator('#node-timeline .nodeheader').click();assert page.locator('.curveRow').count()==3
    page.locator('#details button').filter(has_text='Add key').click();assert page.locator('.curveRow').count()==4
    page.locator('#compile').click();assert page.locator('#status').inner_text().startswith('Compiled')
    page.locator('#details button').filter(has_text='Set breakpoint').click();assert page.locator('#node-timeline.breakpoint').count()==1
    page.locator('#functions .item').filter(has_text='Compute').click();assert 'sum' in page.locator('#locals').inner_text()
    page.locator('#addLocal').click();page.locator('#dialogBody input').fill('temporary');page.locator('#dialogBody select').select_option('int');page.locator('#dialogOK').click();page.wait_for_timeout(100);assert 'temporary' in page.locator('#locals').inner_text()
    page.locator('#compile').click();assert page.locator('#status').inner_text().startswith('Compiled')
    page.locator('#examplesButton').click();page.locator('#dialogBody select').select_option('Gameplay');page.locator('#dialogOK').click();page.wait_for_timeout(120);assert 'Scored' in page.locator('#dispatchers').inner_text()
    page.locator('#dispatchers .item').click();assert 'Create handler' in page.locator('#details').inner_text()
    page.locator('#examplesButton').click();page.locator('#dialogBody select').select_option('Inheritance');page.locator('#dialogOK').click();page.wait_for_timeout(120);assert 'BP_Base' in page.locator('#parentInfo').inner_text();assert 'baseScore' in page.locator('#variables').inner_text()
    page.locator('#functions .item').filter(has_text='GetScore').click();page.wait_for_timeout(120);page.locator('#compile').click();assert page.locator('#status').inner_text().startswith('Compiled')
    page.locator('#examplesButton').click();page.locator('#dialogBody select').select_option('Interfaces');page.locator('#dialogOK').click();page.wait_for_timeout(120)
    assert page.locator('#status').inner_text().startswith('Compiled');assert 'Damageable' in page.locator('#interfaces').inner_text()
    page.locator('#interfaces .item').filter(has_text='Damageable').click();assert 'ApplyDamage' in page.locator('#details').inner_text()
    with page.expect_download() as info:page.locator('#details button').filter(has_text='Export interface').click()
    contract=json.load(open(info.value.path()));assert contract['kind']=='BlueprintInterface';assert contract['methods'][0]['name']=='ApplyDamage'
    page.locator('#addInterface').click();page.locator('#dialogBody input').nth(0).fill('Interactable');page.locator('#dialogBody input').nth(1).fill('Interact():void');page.locator('#dialogBody select').select_option('Implement here');page.locator('#dialogOK').click();page.wait_for_timeout(120)
    assert 'Interactable' in page.locator('#interfaces').inner_text();assert 'Interact' in page.locator('#functions').inner_text();page.locator('#compile').click();assert page.locator('#status').inner_text().startswith('Compiled')
    page.locator('#examplesButton').click();page.locator('#dialogBody select').select_option('TimelineTracks');page.locator('#dialogOK').click();page.wait_for_timeout(120)
    assert page.locator('#status').inner_text().startswith('Compiled');page.locator('#node-timeline .nodeheader').click();assert 'Position' in page.locator('#details').inner_text()
    page.locator('#details button').filter(has_text='Add track').click();page.locator('#dialogBody input').fill('Scale');page.locator('#dialogBody select').select_option('Vec2');page.locator('#dialogOK').click();page.wait_for_timeout(100)
    assert 'Scale' in page.locator('#node-timeline').inner_text();page.locator('#compile').click();assert page.locator('#status').inner_text().startswith('Compiled')
    page.locator('#details button').filter(has_text='Remove track').click();assert 'Scale' not in page.locator('#node-timeline').inner_text();page.locator('#compile').click();assert page.locator('#status').inner_text().startswith('Compiled')
    page.locator('#examplesButton').click();page.locator('#dialogBody select').select_option('AnimationMachine');page.locator('#dialogOK').click();page.wait_for_timeout(120)
    page.locator('#events .item').filter(has_text='update').click();page.locator('#node-machine .nodeheader').click();assert 'AttackPressed' in page.locator('#details').inner_text()
    page.locator('#details button').filter(has_text='Add state').click();page.locator('#dialogBody input').nth(0).fill('Jump');page.locator('#dialogOK').click();page.wait_for_timeout(100)
    assert 'state_Jump' in page.locator('#node-machine').inner_text();page.locator('#details button').filter(has_text='Add transition').click();page.locator('#dialogBody input').fill('JumpNow');page.locator('#dialogBody select').nth(0).select_option('Idle');page.locator('#dialogBody select').nth(1).select_option('Jump');page.locator('#dialogOK').click();page.wait_for_timeout(100)
    assert 'when_JumpNow' in page.locator('#node-machine').inner_text();page.locator('#compile').click();assert page.locator('#status').inner_text().startswith('Compiled')
    page.locator('#examplesButton').click();page.locator('#dialogBody select').select_option('EntityHierarchies');page.locator('#dialogOK').click();page.wait_for_timeout(120)
    assert page.locator('#status').inner_text().startswith('Compiled');page.locator('#hierarchies .item').filter(has_text='PlayerRig').click();assert 'Player2D' in page.locator('#details').inner_text()
    page.locator('#details button').filter(has_text='Add child').click();page.locator('#dialogBody input').nth(0).fill('Shield');page.locator('#dialogBody input').nth(1).fill('[0,15]');page.locator('#dialogOK').click();page.wait_for_timeout(100)
    assert 'Shield' in page.locator('#details').inner_text();assert 'entity_Shield' in page.locator('#node-spawn2').inner_text();page.locator('#compile').click();assert page.locator('#status').inner_text().startswith('Compiled')
    page.locator('#details button').filter(has_text='Edit visual').click();page.locator('#dialogBody select').select_option('rect');page.locator('#dialogOK').click();page.wait_for_timeout(100);assert 'Visual: rect' in page.locator('#details').inner_text()
    page.locator('#details button').filter(has_text='Attach Behaviour').click();page.locator('#dialogOK').click();page.wait_for_timeout(100);assert 'BP_Spinner' in page.locator('#details').inner_text();page.locator('#compile').click();assert page.locator('#status').inner_text().startswith('Compiled')
    with page.expect_download() as info:page.locator('#details button').filter(has_text='Export hierarchy').click()
    h=json.load(open(info.value.path()));assert len(h['hierarchy']['entities'])==4
    page.locator('#projectManager').click();assert page.locator('#projectDialog').is_visible()
    page.locator('#projectScreenFile').set_input_files(str(root/'examples/arcade.bp.json'));page.wait_for_timeout(150)
    assert page.locator('#projectDialog .projectScreen').count()>=2
    with page.expect_download() as info:page.locator('#projectDialog button').filter(has_text='Package release').click()
    dl=info.value; z=zipfile.ZipFile(dl.path());assert z.testzip() is None;assert len([n for n in z.namelist() if n.endswith('main.as')])==2
    with page.expect_download() as info:page.locator('#projectDialog button').filter(has_text='Save project').click()
    p=json.load(open(info.value.path()));assert len(p['screens'])==2
    page.locator('#projectDialog button').filter(has_text='Close').click()
    page.locator('#debugTabButton').click();assert page.locator('#debugPanel').is_visible()
    page.locator('#debugConnect').click();assert 'Start node debug_bridge' in page.locator('#debugStatus').inner_text()
    with page.expect_download() as info:page.locator('#debugExport').click()
    assert '[BP]' in Path(info.value.path()).read_text()
    page.locator('#diagTab').click();page.locator('#examplesButton').click();page.locator('#dialogBody select').select_option('Gameplay');page.locator('#dialogOK').click();page.wait_for_timeout(120)
    page.screenshot(path=str(work/'preview.png'))
    assert not errors, errors
    print('PASS advanced browser: entity hierarchies, state machines, interfaces, typed tracks, examples, timeline editor, breakpoint, locals, dispatcher, inheritance override, multi-screen ZIP package, project save and debug export; no JS errors')
    browser.close()
