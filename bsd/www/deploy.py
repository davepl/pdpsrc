#!/usr/bin/env python3
"""Upload a source bundle from a modern host, without storing credentials."""
import argparse
import ftplib
import getpass
from pathlib import Path
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent
FILES = (
    "Makefile", "install.sh", "backup.sh", "deploy.py", "README.md",
    "README.webtop", "README.visitors", "VERIFICATION.md", "webtop.c",
    "webtop-cgi.c", "visit-counter.c", "site/index.html",
    "site/index.previous.html", "site/pdp11.jpg", "server/httpd.c",
    "server/Makefile", "server/LICENSE", "config/inetd.http",
    "config/services.http", "tests/visitors.js",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", help="PDP's LAN address")
    parser.add_argument("--output", type=Path, help="Create a bundle only; do not connect")
    args = parser.parse_args()
    for name in FILES:
        if not (ROOT / name).is_file():
            parser.error("Missing source file: " + name)
    with tempfile.TemporaryFile() as bundle:
        with tarfile.open(fileobj=bundle, mode="w", format=tarfile.USTAR_FORMAT) as archive:
            for name in FILES:
                archive.add(ROOT / name, arcname=name)
        bundle.seek(0)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(bundle.read())
            print("Created", args.output)
            return
        password = getpass.getpass("root@" + args.host + " FTP password: ")
        with ftplib.FTP() as ftp:
            ftp.connect(args.host, timeout=30)
            ftp.login("root", password)
            # Fixed path outside the document root. One transfer, no load burst.
            ftp.storbinary("STOR /tmp/webtop-source.tar", bundle)
    print("Uploaded /tmp/webtop-source.tar. At the PDP root console, run:")
    print("  test -d /usr/src/local/webtop || mkdir -p /usr/src/local/webtop")
    print("  cd /usr/src/local/webtop")
    print("  tar xf /tmp/webtop-source.tar")
    print("  make clean && make && make install")
    print("  rm /tmp/webtop-source.tar")


if __name__ == "__main__":
    main()
