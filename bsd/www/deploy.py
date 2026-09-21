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
    "visit-proxy.c", "site/health.txt", "proxy/visitor-service.py",
    "config/pdp-visitors.service", "tests/shared-counter.py", "tests/failover.py",
    "deployments/2026-09-21-automatic-failover.md",
    "site/index.previous.html", "site/pdp11.jpg", "server/httpd.c",
    "server/Makefile", "server/LICENSE", "config/inetd.http",
    "config/services.http", "tests/visitors.js", "site/index.source.html",
    "minify.mjs", "package.json", "package-lock.json", "deploy-page.py",
    "config/varnish.vcl", "config/README.varnish.md", "tests/varnish.py",
    "site/pdp1183-web.jpg", "archive/README.md",
    "archive/pre-webtop-192.168.1.26/index.html",
    "archive/pre-webtop-192.168.1.26/pdp1183.jpg",
    "archive/amber-webtop/index.html", "archive/amber-webtop/index.source.html",
    "config/rc.inetd", "config/README.inetd.md",
    "config/Caddyfile.pdp",
    "config/pdp-backend.py",
    "deployments/2026-09-21-192.168.1.29.md",
    "deployments/2026-09-21-public-cutover.md",
    "site/tmog-banner-v2.jpg", "archive/tmog-banner.original.png",
    "archive/tmog-banner.first.png",
)


def source_metadata(member):
    """Use native root ownership, not the development machine's uid/gid."""
    member.uid = member.gid = 0
    member.uname, member.gname = "root", "wheel"
    member.mode = 0o755 if member.mode & 0o111 else 0o644
    return member


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
                archive.add(ROOT / name, arcname=name, filter=source_metadata)
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
            ftp.storbinary("STOR /usr/src/local/webtop-source.tar", bundle)
    print("Uploaded /usr/src/local/webtop-source.tar. At the PDP root console, run:")
    print("  test -d /usr/src/local/webtop || mkdir -p /usr/src/local/webtop")
    print("  cd /usr/src/local/webtop")
    print("  tar xf /usr/src/local/webtop-source.tar")
    print("  make clean && make && sh install.sh")
    print("  rm /usr/src/local/webtop-source.tar")


if __name__ == "__main__":
    main()
