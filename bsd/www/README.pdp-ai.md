# UNIX-Gary / PDP Gary

The public page is <https://pdp1173.com/pdp-ai.html> (`/pdp-ai` redirects there).
Its title is **UNIX-Gary**; the assistant introduces itself as **PDP Gary**.
The green terminal UI includes streaming replies, per-tab conversation history,
Stop and New conversation controls, and a compact mobile layout. Gary's persona
is a grumpy UNIX wizard who gives practical answers using traditional tools,
with occasional UNIX evangelism and accurately qualified Linux/kernel jokes.

## Request paths and failover

Static HTML and images take this path:

```
Browser -> Cloudflare -> Caddy -> Varnish -> PDP .29 (primary)
                                       -> PDP .26 (fallback)
```

Both PDPs store the same five files in `/home/www`:

| File | Purpose |
| --- | --- |
| `pdp-ai.html` | Complete page, CSS and browser JavaScript |
| `gary-green-v1.jpg` | Left-column Gary portrait |
| `unix-gary-apple-touch-icon-v2.png` | 180 × 180 Apple touch icon |
| `unix-gary-favicon-v2.png` | 32 × 32 browser icon |
| `unix-gary-preview-v1.jpg` | 972 × 972 Open Graph link-preview image |

The HTML uses absolute public URLs for preview metadata. Apple and other
clients may cache an older preview. The high-resolution green source artwork
is preserved in `archive/gary-green-original.png`; it was recolored from the
owner-supplied Gary illustration. Web derivatives crop and resize that artwork.
The legacy HTTP server labels PNG as text/plain; the Caddy route corrects
Content-Type to image/png for the two icon paths in public responses.

Chat has a separate path: `/pdp-ai-api/chat` goes through Caddy directly to the
Python bridge on ubvmdell (`192.168.0.107:8091`), then to its existing local
model on `127.0.0.1:8080`. It bypasses Varnish and sends `Cache-Control: no-store`.
The PDP serves the frontend; it does not perform inference. Direct LAN visits
to either PDP send chat to the public HTTPS API, with explicit allowed origins.

The existing `auto` Varnish mode prefers `.29`, falls back to `.26`, and returns
to `.29` on recovery, exactly as for the homepage. Static cache hits and grace
can still serve an earlier copy; backend choice applies to origin fetches.
Chat remains dependent on Caddy and ubvmdell even if the PDP frontend fails over.
See `config/README.varnish.md` for probes, bounded retries and cache behavior.

## Publish or restore the page

From this directory on a modern machine with LAN access:

```
python3 deploy-chat.py 192.168.1.29
python3 deploy-chat.py 192.168.1.26
```

Each invocation prompts for the existing root FTP password. It reads all local
files first, backs up existing copies under the ignored `backups/` directory,
publishes assets before HTML using verified temporary files and atomic renames,
sets mode 644, then verifies direct HTTP byte for byte. It attempts to restore
changed files on a failed verification. Keep the backup if a network failure
also interrupts rollback. No credentials are stored in the source or arguments.

These commands update only the five static chat files. For a complete native
restoration, `deploy.py` includes all chat source and assets in its source
bundle, and `install.sh` publishes the static files after taking its usual
runtime backup. Install the modern bridge and Caddy route separately below.
No Python, Node.js or AI runtime is installed on the PDP.

On caddy, after both uploads, invalidate the updated paths:

```
varnishadm ban 'req.url ~ ^/(pdp-ai[.]html|gary-green-v1[.]jpg|unix-gary-)'
pdp-backend status
```

If the saved mode is not `auto`, use `pdp-backend auto`. Do not force `26` or
`29` for ordinary operation, because those modes disable automatic fallback.

## Restore the AI bridge on ubvmdell

The bridge uses Python 3's standard library. Copy `pdp-ai/server.py` to
`~/.local/share/pdp-ai/server.py` and `pdp-ai/pdp-ai.service` to
`~/.config/systemd/user/pdp-ai.service`, as `dave` on ubvmdell. Create those
directories if absent. The existing llama-coding user service must provide
the `qwen3.8-flash-next` model on port 8080. The unit binds the bridge to
`192.168.0.107`, and the bridge uses port 8091 by default.

Preserve these private credential files outside Git:

* `~/.config/llama-coding/api-key`: the existing local model API credential.
* `~/.config/pdp-ai/proxy-key`: a separate random key shared only with Caddy.

For a new installation, create the proxy credential in a private directory
with `umask 077` and `openssl rand -hex 32`. Do not rotate an existing working
key unless updating Caddy at the same time. Credential files should be mode
600. Then run `systemctl --user daemon-reload` and
`systemctl --user enable --now pdp-ai.service`. User lingering is needed for
startup without an interactive login; it is already enabled on ubvmdell.

The bounded bridge accepts only alternating text messages. It rejects supplied
system prompts/tool fields, limits input to 2,000 characters and recent context
to 25 messages / 24,000 characters, allows one generation at a time, caps replies
at 1,024 tokens, and applies per-client/global request limits. It has no shell
or agent tools. It does not log prompts, answers, client IPs or credentials.

## Restore the Caddy route

Copy `config/pdp-ai-route.caddy.template` to `/etc/caddy/pdp-ai-route.caddy` on
caddy. Replace `PROXY_KEY_PLACEHOLDER` there with the same private proxy key;
use root ownership, group `caddy`, and mode 640. Merge the import shown in
`config/Caddyfile.pdp` into the existing PDP host block, preserving other sites.
The template itself contains no working credential.

The route limits request bodies, forwards only the dedicated bridge, and uses
Cloudflare's connecting-IP header for per-client rate accounting. Preserve the
existing Cloudflare trust/network setup; this header is not visitor
authentication. The shared secret authenticates Caddy to the private bridge.

Validate with `caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile`,
then reload Caddy. Keep ports 8080 and 8091 private; expose only the public
frontend route. `/pdp-ai-api/health` verifies bridge/model availability without
submitting a prompt. The Python service is independent of the native PDP build.

## Verify

```
cd pdp-ai
python3 -m unittest -v test_server
```

Tests use a fake local model and temporary credentials. They check proxy
authentication, both PDP origins and the public origin, rejected origins,
bounded roles/context, concurrency, rate limiting, streaming and follow-up
history. Run `python3 tests/failover.py` from the parent directory on Linux
with Varnish to test real probes, primary/fallback/recovery, and chat assets
using isolated fake origins. It never contacts the PDPs.

For a serial live route check through Caddy, send `Host: pdp1173.com` and a
temporary request cookie so Varnish passes the cache. Verify `X-PDP-Node`,
`X-Cache: PASS`, and exact bytes for the page and all four images. During a
controlled test, mark the active VCL's `.pdp29` backend `sick`, check `.26`,
then immediately restore its admin health to `auto` and check recovery.
Always arrange restoration in a `finally` handler; do not stop either PDP.
The September 23 live checks are recorded in `deployments/2026-09-23-unix-gary.md`.
