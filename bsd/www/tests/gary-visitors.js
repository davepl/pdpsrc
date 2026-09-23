const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {webcrypto} = require('node:crypto');
const html = fs.readFileSync(path.join(__dirname, '../site/pdp-ai.html'), 'utf8');
const source = html.match(/<script id="pdp-visitor-script">([\s\S]*?)<\/script>/)[1];
assert.ok(!/unix[- ]gary|UNIX<span>/i.test(html));
assert.match(html, /<title>PDP-Gary<\/title>/);
for (const match of html.matchAll(/(?:src|href)="\/?([^"/:]+\.(?:png|jpg))"/g)) {
    assert.ok(fs.existsSync(path.join(__dirname, '../site', match[1])), match[1]);
}

async function visit({jar = {}, storage = {}, cookies = true, blocked = false,
                      reload = false, hidden = false, fail = false,
                      hostname = 'pdp1173.com'} = {}) {
    const display = {'visitor-total': {}, 'visitor-current': {}};
    const calls = [], listeners = {}, timers = new Map();
    let next = 0;
    const document = {
        hidden,
        getElementById: id => display[id],
        addEventListener: (name, fn) => {listeners[name] = fn;},
        get cookie() {return cookies ? Object.entries(jar).map(([k,v]) => k+'='+v).join('; ') : '';},
        set cookie(value) {if (cookies) {const [k,v] = value.split(';')[0].split('='); jar[k]=v;}},
    };
    const sandbox = {
        document, AbortController, crypto: webcrypto, location: {hostname},
        performance: {getEntriesByType: () => [{type: reload ? 'reload' : 'navigate'}]},
        sessionStorage: {
            getItem(k) {if (blocked) throw Error('blocked'); return storage[k];},
            setItem(k,v) {if (blocked) throw Error('blocked'); storage[k]=v;},
        },
        addEventListener: (name, fn) => {listeners[name] = fn;},
        setTimeout(fn, delay) {timers.set(++next, {fn, delay}); return next;},
        clearTimeout(id) {timers.delete(id);},
        async fetch(url, options) {
            calls.push({url, ...options, body: options.body && JSON.parse(options.body)});
            if (fail) throw Error('offline');
            return {ok: true, json: async () => ({total: 12345, current: 2})};
        },
    };
    sandbox.window = sandbox;
    vm.runInNewContext(source, sandbox);
    const settle = () => new Promise(resolve => setImmediate(resolve));
    await settle();
    return {calls, display, document, listeners, timers, settle};
}
(async () => {
    const jar = {pdp11_visit_v1: '1'};
    const first = await visit({jar});
    assert.equal(first.calls[0].body.increment, true, 'Homepage visits do not suppress Gary visits');
    const token = first.calls[0].body.session;
    assert.equal(first.display['visitor-total'].textContent, '12,345');
    assert.equal(first.display['visitor-current'].textContent, '2');
    assert.equal(jar.pdp11_visit_v1, '1');
    for (const reload of [false, true]) {
        const again = await visit({jar, reload});
        assert.equal(again.calls[0].body.session, token, 'Tabs share the session');
        assert.equal(again.calls[0].body.increment, false);
    }
    assert.equal((await visit({reload: true})).calls[0].body.increment, true,
                 'First Gary visit counts even when first loaded via refresh');
    const storage = {};
    assert.equal((await visit({cookies: false, storage})).calls[0].body.increment, true);
    assert.equal((await visit({cookies: false, storage})).calls[0].body.increment, false);
    const blocked = await visit({cookies: false, blocked: true});
    assert.equal(blocked.calls[0].url, '/pdp-visitors/stats');
    assert.equal(blocked.calls[0].body, undefined);
    const lan = await visit({hostname:'192.168.1.26'});
    assert.equal(lan.calls[0].url, 'https://pdp1173.com/pdp-visitors/heartbeat');
    const poll = [...first.timers.values()].find(t => t.delay === 30000);
    assert.ok(poll);
    await poll.fn();
    assert.equal(first.calls[1].body.increment, false);
    first.document.hidden = true;
    first.listeners.visibilitychange();
    assert.equal(first.timers.size, 0);
    first.document.hidden = false;
    first.listeners.visibilitychange();
    await first.settle();
    assert.equal(first.calls.length, 3);
    const hidden = await visit({hidden: true});
    assert.equal(hidden.calls.length, 0);
    const failed = await visit({fail: true});
    assert.equal(failed.display['visitor-total'].textContent, '—');
    assert.equal(failed.display['visitor-current'].textContent, '—');
    await [...failed.timers.values()].find(t => t.delay === 30000).fn();
    assert.equal(failed.calls[1].body.session, failed.calls[0].body.session);
    assert.equal(failed.calls[1].body.increment, true, 'Retry remains idempotent on the server');
    console.log('PASS PDP-Gary: branding, assets, independent sessions, tabs, reloads, storage, LAN, visibility, polling, outages');
})().catch(error => {console.error(error); process.exitCode = 1;});
