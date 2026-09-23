// Exercise the shipped animation with a clock and DOM stand-ins; no PDP traffic.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function run(html, reduced = false) {
    const nodes = new Map();
    for (const match of html.matchAll(/\bid=(?:"([^"]+)"|([^\s>]+))/g)) {
        nodes.set(match[1] || match[2], {style: {}, listeners: {}, textContent: '',
            addEventListener(name, fn) { this.listeners[name] = fn; },
            setAttribute(name, value) { this[name] = value; }});
    }
    const stage = nodes.get('vp-stage'), panel = {style: {}};
    stage.clientWidth = 1000;
    stage.querySelector = () => panel;
    const handlers = {}, mediaHandlers = {}, timers = new Map();
    let now = 0, serial = 0, intersect, resize;
    const document = {hidden: false, getElementById: id => nodes.get(id),
        addEventListener: (name, fn) => { handlers[name] = fn; }};
    const context = {document, performance: {now: () => now}, Math: Object.create(Math),
        matchMedia: () => ({matches: reduced, addEventListener: (name, fn) => { mediaHandlers[name] = fn; }}),
        setTimeout: fn => { timers.set(++serial, fn); return serial; },
        clearTimeout: id => timers.delete(id),
        ResizeObserver: class { constructor(fn) { resize = fn; } observe() {} },
        IntersectionObserver: class { constructor(fn) { intersect = fn; } observe() {} },
        fetch() { throw Error('A simulated panel must not make network requests'); }};
    context.Math.random = () => 0.5;
    context.window = context;
    const scripts = [...html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)];
    const animation = scripts.find(s => /id=(?:"virtual-panel-script"|virtual-panel-script)/.test(s[0]));
    assert.ok(animation, 'The panel animation is included in the served HTML');
    vm.runInNewContext(animation[1], context);
    function advance(ms) {
        for (let i = 0; i < Math.ceil(ms / 34); i++) {
            now += 34;
            const pending = [...timers.values()]; timers.clear();
            pending.forEach(fn => fn());
            assert.ok(timers.size <= 1, 'At most one animation timer');
        }
    }
    function dataWord() {
        let word = 0;
        for (let bit = 0; bit < 16; bit++) if (nodes.get('vp-d' + bit).style.opacity === '1') word |= 1 << bit;
        return word;
    }
    return {nodes, stage, panel, document, handlers, mediaHandlers, timers, advance, dataWord,
        intersect: value => intersect([{isIntersecting: value}]), resize: () => resize()};
}

for (const filename of ['index.source.html', 'index.html']) {
    const html = fs.readFileSync(path.join(__dirname, '../site', filename), 'utf8');
    const main = html.slice(html.indexOf('<main'), html.indexOf('</main>'));
    assert.ok(main.indexOf('webtop-panel') < main.indexOf('virtual-panel'));
    assert.ok(main.indexOf('virtual-panel') < main.indexOf('tmog-banner'));
    assert.equal(new Set([...html.matchAll(/\bid=(?:"([^"]+)"|([^\s>]+))/g)].map(m => m[1] || m[2])).size,
        [...html.matchAll(/\bid=(?:"([^"]+)"|([^\s>]+))/g)].length, 'IDs stay unique');
    assert.ok(!/onclick=|src=.?(?:pdp11|bootcode|iopage)\.js/.test(html), 'No emulator or machine control handlers');
    const t = run(html), button = t.nodes.get('vp-pause');
    assert.equal(t.dataWord(), 0x00ff);
    t.advance(34); assert.equal(t.dataWord(), 0x01fe);
    t.advance(34 * 7); assert.equal(t.dataWord(), 0xff01, 'BSD tests the new sign bit, not the old carry');
    t.advance(13900); assert.match(t.nodes.get('vp-state').textContent, /Simulated activity/);
    t.advance(2500); assert.match(t.nodes.get('vp-state').textContent, /Simulated 2.11BSD idle/);
    button.listeners.click();
    const pausedWord = t.dataWord(); t.advance(1000);
    assert.equal(t.dataWord(), pausedWord); assert.equal(t.timers.size, 0);
    assert.equal(button['aria-pressed'], 'true');
    button.listeners.click(); assert.equal(t.timers.size, 1);
    t.document.hidden = true; t.handlers.visibilitychange(); assert.equal(t.timers.size, 0);
    t.document.hidden = false; t.handlers.visibilitychange(); assert.equal(t.timers.size, 1);
    t.intersect(false); assert.equal(t.timers.size, 0);
    t.intersect(true); assert.equal(t.timers.size, 1);
    t.stage.clientWidth = 320; t.resize(); assert.equal(t.panel.style.transform, 'scale(' + 320 / 720 + ')');
    const m = run(html, true); assert.equal(m.timers.size, 0, 'Reduced motion starts paused');
    m.nodes.get('vp-pause').listeners.click(); assert.equal(m.timers.size, 1, 'Explicit resume works');
    m.mediaHandlers.change({matches: true}); assert.equal(m.timers.size, 0);
    console.log(`PASS ${filename}: BSD idle, activity/recovery, placement, scaling, pause, hidden/offscreen, reduced motion, no network`);
}
