#!/usr/bin/env python3
"""Upload and build in a temporary 2.11BSD tree using FTP and Telnet.

Password is read from PDP11_PASSWORD or the terminal, never stored/logged.
This does not install, start, or reconfigure a service. Telnet/FTP are cleartext;
use only on the explicitly authorized laboratory network.
"""
import argparse
import ftplib
import getpass
import hashlib
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("probe", "build", "crypto", "wire", "password", "flags"))
    parser.add_argument("--host", required=True)
    parser.add_argument("--user", required=True)
    parser.add_argument("--ftp-port", type=int, default=21)
    parser.add_argument("--telnet-port", type=int, default=23)
    parser.add_argument("--remote", default="/usr/tmp/smbd-test")
    parser.add_argument("--incremental", action="store_true",
                        help="build only: upload changed source bytes and run make without clean")
    args = parser.parse_args()
    if args.incremental and args.action != "build":
        parser.error("--incremental applies only to build")
    if (not re.fullmatch(r"/(?:usr/)?tmp/[A-Za-z0-9_.\-/]+", args.remote)
            or ".." in args.remote.split("/")):
        parser.error("--remote must be a simple path below /tmp or /usr/tmp")
    password = os.environ.get("PDP11_PASSWORD")
    if password is None:
        password = getpass.getpass("Target password: ")
    here = Path(__file__).resolve().parent
    source = here.parent
    destination = args.remote + "/bsd/smbd"
    if args.action == "build":
        uploads = [(p, destination + "/" + p.name)
                   for p in sorted(source.glob("*.c")) + sorted(source.glob("*.h"))]
        uploads += [(source / "Makefile", destination + "/Makefile"),
                    (source.parent / "pdp11_unistd.h", args.remote + "/bsd/pdp11_unistd.h")]
        commands = (("" if args.incremental else "make clean && ") +
                    "make && size smbd smbpwd && ls -l smbd smbpwd")
    elif args.action == "password":
        uploads = [(source / name, destination + "/" + name)
                   for name in ("smbpwd.c", "crypto.c", "wire.c", "smbd.h")]
        uploads += [(source.parent / "pdp11_unistd.h", args.remote + "/bsd/pdp11_unistd.h")]
        commands = "cc -O -i -o smbpwd smbpwd.c crypto.c wire.c && size smbpwd"
    elif args.action == "flags":
        uploads = [(here / "target-flags-test.c", destination + "/tests/target-flags-test.c")]
        commands = ("cc -O -i -o flags-test tests/target-flags-test.c && ./flags-test")
    elif args.action == "crypto":
        uploads = [(here / "crypto_test.c", destination + "/tests/crypto_test.c")]
        commands = ("cc -O -i -o crypto-test tests/crypto_test.c crypto.c wire.c && "
                    "./crypto-test && size crypto-test")
    elif args.action == "wire":
        uploads = [(here / "target-wire-test.c", destination + "/tests/target-wire-test.c")]
        commands = ("cc -O -i -o wire-test tests/target-wire-test.c wire.c && "
                    "./wire-test && size wire-test")
    else:
        uploads = [(here / "target-probe.c", destination + "/target-probe.c")]
        commands = ("uname -a\nhead -2 /VERSION\nsysctl hw.model\n"
                    "sysctl hw.machine\nsysctl hw.physmem\n"
                    "cc -i -o target-probe target-probe.c && ./target-probe && size target-probe")
    script = ("#!/bin/sh\ncd " + destination + " || exit 1\n" + commands +
              "\nrc=$?\necho __SMBD_TARGET_DONE_${rc}\nexit $rc\n")
    # Read once before network I/O: the native compiler must see one coherent
    # snapshot, not files still being edited during a slow FTP upload.
    stamps = {local: (local.stat().st_mtime_ns, local.stat().st_size,
                      local.stat().st_ino) for local, remote in uploads}
    snapshot = [(remote, local.read_bytes()) for local, remote in uploads]
    for local, remote in uploads:
        current = local.stat()
        if stamps[local] != (current.st_mtime_ns, current.st_size, current.st_ino):
            raise RuntimeError("Source changed while snapshotting: " + str(local))
    digest = hashlib.sha256()
    for remote, content in snapshot:
        digest.update(remote.encode("utf-8") + b"\0" + content)
    print("Native source snapshot SHA256: " + digest.hexdigest(), flush=True)
    with ftplib.FTP() as ftp:
        ftp.connect(args.host, args.ftp_port, timeout=30)
        ftp.login(args.user, password)
        for directory in (args.remote, args.remote + "/bsd", destination, destination + "/tests"):
            try:
                ftp.mkd(directory)
            except ftplib.error_perm:
                # Verify an existing directory, rather than hiding permission errors.
                ftp.cwd(directory)
        changed = 0
        for remote, content in snapshot:
            if args.incremental:
                prior = io.BytesIO()
                try:
                    ftp.retrbinary("RETR " + remote, prior.write)
                except ftplib.error_perm as error:
                    if not str(error).startswith("550"):
                        raise
                else:
                    if prior.getvalue() == content:
                        continue
            ftp.storbinary("STOR " + remote, io.BytesIO(content))
            changed += 1
        print("Uploaded %d of %d source files" % (changed, len(snapshot)), flush=True)
        ftp.storbinary("STOR " + args.remote + "/run-smbd.sh", io.BytesIO(script.encode("ascii")))
    for executable in ("expect", "telnet"):
        if not shutil.which(executable):
            raise RuntimeError("Required command unavailable: " + executable)
    child_environment = os.environ.copy()
    child_environment.update(SMBD_TARGET_HOST=args.host, SMBD_TARGET_USER=args.user,
                             SMBD_TARGET_PASSWORD=password,
                             SMBD_TARGET_PORT=str(args.telnet_port),
                             SMBD_TARGET_SCRIPT=args.remote + "/run-smbd.sh")
    expect_script = r"""
set timeout 30
log_user 0
spawn telnet $env(SMBD_TARGET_HOST) $env(SMBD_TARGET_PORT)
expect {
    -re {login:} { send -- "$env(SMBD_TARGET_USER)\r" }
    timeout { puts stderr "No target login prompt"; exit 1 }
    eof { puts stderr "Target disconnected before login"; exit 1 }
}
expect {
    -re {Password:} { send -- "$env(SMBD_TARGET_PASSWORD)\r" }
    timeout { puts stderr "No target password prompt"; exit 1 }
    eof { puts stderr "Target disconnected before authentication"; exit 1 }
}
expect {
    -re {(?i)login incorrect} { puts stderr "Target rejected login"; exit 1 }
    -re {[>#\$] $} {}
    timeout { puts stderr "No target shell prompt"; exit 1 }
    eof { puts stderr "Target disconnected after authentication"; exit 1 }
}
unset env(SMBD_TARGET_PASSWORD)
log_user 1
send -- "sh $env(SMBD_TARGET_SCRIPT)\r"
set timeout 600
expect {
    -re {__SMBD_TARGET_DONE_([0-9]+)} { set status $expect_out(1,string) }
    timeout { puts stderr "Target build timed out"; exit 1 }
    eof { puts stderr "Target disconnected during build"; exit 1 }
}
send -- "exit\r"
expect eof
exit $status
"""
    return subprocess.run(["expect", "-c", expect_script], env=child_environment).returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ftplib.Error) as error:
        sys.exit("target.py: " + str(error))
