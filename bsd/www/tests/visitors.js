// Exercise the actual inline counter script without contacting the PDP.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../site/index.html'), 'utf8');
const source = html.match(/\(function\(\)\{[\s\S]*?loadCount\(\);\n\}\)\(\);/)[0];

async function visit({reload = false, cookies = true, storage = true,
                      jar = {}, session = {}, fail = false} = {}) {
    const display = {textContent: ''};
    const calls = [];
    const document = {
        getElementById: () => display,
        get cookie() { return cookies ? (jar.cookie || '') : ''; },
        set cookie(value) { if (cookies) jar.cookie = value.split(';')[0]; },
    };
    const sandbox = {
        document, AbortController,
        performance: {getEntriesByType: () => [{type: reload ? 'reload' : 'navigate'}]},
        sessionStorage: {
            getItem(key) { if (!storage) throw Error('blocked'); return session[key]; },
            setItem(key, value) { if (!storage) throw Error('blocked'); session[key] = value; },
        },
        setTimeout: () => 1, clearTimeout: () => {},
        async fetch(url) {
            calls.push(url);
            if (url === '/cgi-bin/visit') {
                assert.ok(jar.cookie || session.pdp11_visit_v1,
                          'Reserve the session before an increment is sent');
            }
            if (fail) throw Error('interrupted response');
            return {ok: true, text: async () => '0000012345\n'};
        },
    };
    sandbox.window = sandbox;
    vm.runInNewContext(source, sandbox);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(calls.length, 1, 'Exactly one counter request per page load');
    assert.equal(display.textContent, fail ? 'VISITORS: unavailable' : 'VISITORS: 12,345');
    return calls[0];
}

(async () => {
    const increment = '/cgi-bin/visit', read = '/visits.txt';
    const jar = {}, session = {};
    assert.equal(await visit({jar, session}), increment);
    assert.equal(await visit({jar, session, reload: true}), read);
    assert.equal(await visit({jar}), read, 'A new tab shares the session cookie');
    assert.equal(await visit({reload: true}), read, 'Reload without prior marker must not increment');
    assert.equal(await visit(), increment, 'An independent session counts');
    const fallback = {};
    assert.equal(await visit({cookies: false, session: fallback}), increment);
    assert.equal(await visit({cookies: false, session: fallback}), read);
    assert.equal(await visit({cookies: false, storage: false}), read);
    const failedJar = {};
    assert.equal(await visit({jar: failedJar, fail: true}), increment);
    assert.equal(await visit({jar: failedJar}), read, 'Interrupted response must not cause a second increment');
    console.log('PASS: new sessions, reloads, new tabs, blocked storage, formatting, interrupted responses');
})().catch(error => { console.error(error); process.exitCode = 1; });
