"""Optional real-browser checks; normal preview use needs no Playwright."""
import argparse
import asyncio
import json
from pathlib import Path
from urllib.parse import urlsplit


async def check(args):
    from playwright.async_api import async_playwright

    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    async with async_playwright() as playwright:
        options = {'headless': True}
        if args.browser_binary:
            options['executable_path'] = str(args.browser_binary)
        if args.no_sandbox:
            options['args'] = ['--no-sandbox']
        browser = await playwright.chromium.launch(**options)
        page = await browser.new_page(viewport={'width': 1180, 'height': 900})
        errors = []
        page.on('pageerror', lambda error: errors.append(str(error)))

        async def ready():
            await page.goto(args.url)
            await page.evaluate('window.tandoPreview.ready')
            await page.wait_for_timeout(150)

        async def progress():
            return await page.locator('#serial-log').text_content()

        await ready()
        manifest = await page.evaluate('window.tandoPreview.manifest')
        for eye in ('eye-left', 'eye-right'):
            assert await page.locator('#' + eye).evaluate('''canvas => {
                const bytes=canvas.getContext('2d').getImageData(0,0,240,240).data;
                return bytes.some((v,i)=>i%4!==3 && v>0);
            }'''), 'Both eyes must contain rendered pixels'
        start = await page.evaluate('({host:performance.now(),...window.tandoPreview.metrics})')
        await page.wait_for_timeout(args.seconds * 1000)
        end = await page.evaluate('({host:performance.now(),...window.tandoPreview.metrics})')
        elapsed = end['host'] - start['host']
        pairs = end['firmwareFrames'] - start['firmwareFrames']
        clock_ratio = (end['firmwareTimeMs'] - start['firmwareTimeMs']) / elapsed
        assert pairs > 0 and .95 < clock_ratio < 1.05, (pairs, clock_ratio)

        await page.locator('[data-command="p"]').click()
        await page.wait_for_function("document.getElementById('serial-log').textContent.includes('PET -> +1 PROGRESS')")
        await ready()
        assert 'Progress credits: 1 / 9' in await progress()
        alternate = {**manifest, 'preview_id': 'qa-' + manifest['preview_id'],
                     'branch': manifest['branch'] + '/qa'}

        async def other_profile(route):
            await route.fulfill(json=alternate)

        await page.route('**/manifest.json', other_profile)
        await ready()
        assert 'Progress credits: 0 / 9' in await progress()
        await page.unroute('**/manifest.json', other_profile)
        await ready()
        assert 'Progress credits: 1 / 9' in await progress()
        await page.locator('#pause').click()
        before = await page.evaluate('window.tandoPreview.metrics.firmwareTimeMs')
        await page.wait_for_timeout(350)
        assert before == await page.evaluate('window.tandoPreview.metrics.firmwareTimeMs')
        assert await page.locator('[data-command="p"]').is_disabled()
        await page.locator('#pause').click()
        await page.wait_for_timeout(200)
        assert before < await page.evaluate('window.tandoPreview.metrics.firmwareTimeMs')
        await page.screenshot(path=str(output / 'desktop.png'), full_page=True)
        await page.set_viewport_size({'width': 390, 'height': 900})
        assert await page.evaluate('document.documentElement.scrollWidth<=innerWidth+1')
        await page.screenshot(path=str(output / 'mobile.png'), full_page=True)

        async def corrupt(route):
            await route.fulfill(body=b'\0asm\1\0\0\0', content_type='application/wasm')

        await page.route('**/preview.wasm', corrupt)
        await page.goto(args.url)
        rejected = await page.evaluate('window.tandoPreview.ready.then(()=>false,()=>true)')
        assert rejected and not await page.evaluate('window.tandoPreview.metrics.initialized')
        assert not errors, errors
        result = {'branch': manifest['branch'], 'preview_id': manifest['preview_id'],
                  'real_pairs_per_s': pairs * 1000 / elapsed, 'clock_ratio': clock_ratio,
                  'checks': ['two rendered eyes', 'real frame/time progress', 'PET and persistence',
                             'branch-profile isolation', 'pause/resume', 'mobile layout',
                             'corrupt WASM rejected before initialization', 'no browser exceptions']}
        (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result, indent=2))
        await browser.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://127.0.0.1:8787/')
    parser.add_argument('--browser-binary', type=Path)
    parser.add_argument('--no-sandbox', action='store_true', help='Only for development containers that require it.')
    parser.add_argument('--seconds', type=float, default=5)
    parser.add_argument('--output-dir', type=Path, default=Path(__file__).parents[1] / '.reports/browser')
    args = parser.parse_args()
    url = urlsplit(args.url)
    if url.scheme != 'http' or url.hostname not in ('127.0.0.1', 'localhost', '::1'):
        parser.error('Use the local preview server URL')
    if not 1 <= args.seconds <= 60:
        parser.error('seconds must be 1..60')
    try:
        asyncio.run(check(args))
    except ModuleNotFoundError as error:
        parser.exit(2, 'Optional browser checks need Playwright: {}\n'.format(error))


if __name__ == '__main__':
    main()
