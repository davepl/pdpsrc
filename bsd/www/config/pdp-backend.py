#!/usr/bin/env python3
"""Select automatic PDP failover or force .26/.29 without restarting Varnish."""
import argparse
import datetime
import fcntl
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid
from urllib.request import Request, build_opener, ProxyHandler

CONFIG = Path('/etc/varnish/default.vcl')
BACKUPS = Path('/root/pdp-varnish-backups')
MODE = re.compile(r'(set req[.]http[.]X-PDP-Mode = )"(auto|26|29)";')
HTTP = build_opener(ProxyHandler({}))


def adm(*args):
    return subprocess.check_output(['varnishadm', *args], text=True).strip()


def active_name():
    return next(line.split()[-1] for line in adm('vcl.list').splitlines()
                if line.split() and line.split()[0] == 'active')


def mode(text):
    matches = list(MODE.finditer(text))
    if len(matches) != 1:
        raise RuntimeError('Expected exactly one X-PDP-Mode setting')
    return matches[0].group(2)


def publish(text, suffix):
    temp = CONFIG.with_name('default.vcl.' + suffix)
    with temp.open('w') as stream:
        stream.write(text); stream.flush(); os.fsync(stream.fileno())
    temp.chmod(0o644)
    os.replace(temp, CONFIG)


def check_counter():
    request = Request('http://127.0.0.1:6081/visits.txt', headers={'Host': 'pdp1173.com'})
    with HTTP.open(request, timeout=10) as response:
        body = response.read(64)
    if not re.fullmatch(rb'[0-9]+\n', body):
        raise RuntimeError('Shared counter unavailable')
    return int(body)


def wait_healthy(name, target):
    deadline = time.monotonic() + 35
    while True:
        rows = adm('backend.list', name + '.*')
        good = set()
        for line in rows.splitlines():
            columns = line.split()
            if columns and columns[0].startswith(name + '.') and 'healthy' in columns:
                good.add(columns[0].split('.')[-1])
        ready = bool(good & {'pdp29', 'pdp26'}) if target == 'auto' else 'pdp' + target in good
        if ready:
            return
        if time.monotonic() >= deadline:
            raise RuntimeError('Selected PDP failed its HTTP health probes\n' + rows)
        time.sleep(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=('auto', '26', '29', 'status'))
    parser.add_argument('--check', action='store_true', help='check current target health; do not switch')
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error('Run as root on the Caddy server')
    with open('/run/lock/pdp-backend.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        previous = active_name(); saved = CONFIG.read_text()
        active_mode = mode(adm('vcl.show', previous)); saved_mode = mode(saved)
        if args.target == 'status':
            print('Active mode: %s; saved mode: %s; VCL: %s' % (active_mode, saved_mode, previous))
            print(adm('backend.list', previous + '.*'))
            print('Shared visitors: ' + str(check_counter()))
            return
        if active_mode != saved_mode:
            raise RuntimeError('Active and saved modes disagree; resolve before switching')
        check_counter()
        wait_healthy(previous, args.target)
        if args.check:
            print('Selected mode healthy; no changes made'); return
        if active_mode == args.target:
            print('Already selected; no changes made'); return
        candidate = MODE.sub(lambda m: m.group(1) + '"' + args.target + '";', saved)
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        name = 'pdp_%s_%s_%s' % (args.target, stamp, uuid.uuid4().hex[:6])
        BACKUPS.mkdir(mode=0o700, exist_ok=True)
        backup = BACKUPS / (name + '.before.vcl'); backup.write_text(saved); backup.chmod(0o600)
        path = CONFIG.with_name(name + '.vcl'); path.write_text(candidate); path.chmod(0o644)
        adm('vcl.load', name, str(path))
        try:
            wait_healthy(name, args.target)
            adm('vcl.use', name)
            publish(candidate, name)
            check_counter()
        except Exception:
            adm('vcl.use', previous)
            publish(saved, name + '.rollback')
            adm('vcl.discard', name)
            raise
        # Retire old configurations so unused probes do not keep polling PDPs.
        for line in adm('vcl.list').splitlines():
            columns = line.split()
            if columns and columns[0] == 'available':
                adm('vcl.discard', columns[-1])
        print('Mode %s saved; auto prefers .29 and falls back to .26.' % args.target)
        print('Previous configuration: ' + str(backup))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        sys.exit('pdp-backend: ' + str(error))
