#!/usr/bin/env python3
"""Select a PDP origin on the Caddy/Varnish host without restarting the cache."""
import argparse
import datetime
import fcntl
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid
from urllib.request import Request, build_opener, ProxyHandler

CONFIG = Path("/etc/varnish/default.vcl")
BACKUPS = Path("/root/pdp-varnish-backups")
HOST = re.compile(r'(backend\s+pdp\s*\{\s*\.host\s*=\s*)"(192\.168\.1\.(?:26|29))"')
HTTP = build_opener(ProxyHandler({}))


def adm(*args):
    return subprocess.check_output(["varnishadm", *args], text=True).strip()


def active_name():
    return next(line.split()[-1] for line in adm("vcl.list").splitlines()
                if line.split() and line.split()[0] == "active")


def origin(text):
    matches = list(HOST.finditer(text))
    if len(matches) != 1:
        raise RuntimeError("Expected exactly one supported backend pdp address")
    return matches[0].group(2)


def health(host, port=80):
    result = {}
    for path in ("/", "/cgi-bin/webtop", "/visits.txt"):
        request = Request("http://%s:%s%s" % (host, port, path),
                          headers={"Host": "pdp1173.com", "Connection": "close"})
        with HTTP.open(request, timeout=20) as response:
            body = response.read(100000)
            if response.status != 200:
                raise RuntimeError("HTTP check failed: " + path)
        if path == "/" and b'webtop' not in body:
            raise RuntimeError("Target is not serving the full application")
        if path == "/cgi-bin/webtop" and b"Tasks:" not in body:
            raise RuntimeError("Target webtop is unavailable")
        if path == "/visits.txt" and not re.fullmatch(rb"[0-9]+\n?", body):
            raise RuntimeError("Target visitor total is unavailable")
        result[path] = body
    return int(result["/visits.txt"])


def publish(text, suffix):
    temp = CONFIG.with_name("default.vcl." + suffix)
    with temp.open("w") as stream:
        stream.write(text)
        stream.flush()
        os.fsync(stream.fileno())
    temp.chmod(0o644)
    os.replace(temp, CONFIG)


def forget_dynamic():
    # Keep the warmed homepage/images, but never show the previous PDP's TOP.
    for path in ("/cgi-bin/webtop", "/visits.txt"):
        adm("ban", "req.url == " + path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=("26", "29", "status"))
    parser.add_argument("--check", action="store_true", help="check target only; do not switch")
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error("Run as root on the Caddy server")
    with open("/run/lock/pdp-backend.lock", "a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        previous = active_name()
        active_host = origin(adm("vcl.show", previous))
        saved = CONFIG.read_text()
        saved_host = origin(saved)
        if args.target == "status":
            print("Active PDP: %s; saved PDP: %s; VCL: %s" % (active_host, saved_host, previous))
            return
        if saved_host != active_host:
            raise RuntimeError("Active and saved origins disagree; resolve before switching")
        target = "192.168.1." + args.target
        print("Checking %s..." % target, flush=True)
        count = health(target)
        print("Target healthy; visitor total: %s" % count, flush=True)
        if args.check:
            return
        if target == active_host:
            print("Already selected; no changes made")
            return
        candidate = HOST.sub(lambda m: m.group(1) + '"' + target + '"', saved)
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        name = "pdp_%s_%s_%s" % (args.target, stamp, uuid.uuid4().hex[:6])
        BACKUPS.mkdir(mode=0o700, exist_ok=True)
        backup = BACKUPS / (name + ".before.vcl")
        backup.write_text(saved)
        backup.chmod(0o600)
        path = CONFIG.with_name(name + ".vcl")
        path.write_text(candidate)
        path.chmod(0o644)
        adm("vcl.load", name, str(path))  # Validate before changing live traffic.
        try:
            adm("vcl.use", name)
            publish(candidate, name)
            forget_dynamic()
            health("127.0.0.1", 6081)
        except Exception:
            adm("vcl.use", previous)
            publish(saved, name + ".rollback")
            forget_dynamic()
            print("Switch failed; restored " + active_host, file=sys.stderr)
            raise
        print("Now serving from %s; persists after reboot" % target)
        print("Switch back: pdp-backend " + active_host.rsplit(".", 1)[1])
        print("Previous configuration: " + str(backup))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        sys.exit("pdp-backend: " + str(error))
