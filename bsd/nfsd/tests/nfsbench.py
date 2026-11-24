#!/usr/bin/env python3
"""Throughput of nfsd: write a file, read it back, compare.

    nfsbench.py [-u uid] [-g gid] [-n kilobytes] host /export
"""
import getopt, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nfstest as t


def main():
    opts, args = getopt.getopt(sys.argv[1:], 'u:g:n:')
    o = dict(opts)
    host, export = args
    uid, gid = int(o.get('-u', os.getuid())), int(o.get('-g', os.getgid()))
    size = int(o.get('-n', 1024)) * 1024
    mn = t.Rpc(host, 2049, t.MNT, 3, uid, gid)
    u = mn.call(1, t.Pack().opaque(export).b)
    assert u.u32() == 0, 'mount refused'
    root = u.opaque()
    n = t.Nfs(t.Rpc(host, 2049, t.NFS, 3, uid, gid))
    io = n.fsinfo(root)[1][0]
    name = 'nfsbench.%d' % os.getpid()
    st, f, a = n.create(root, name, mode=0o644)
    assert st == 0, 'create: %d' % st
    data = os.urandom(size)
    t0 = time.time()
    for off in range(0, size, io):
        st, cnt, _ = n.write(f, off, data[off:off + io])
        assert st == 0 and cnt == len(data[off:off + io])
    n.commit(f)
    t1 = time.time()
    back = b''
    while len(back) < size:
        st, d, eof = n.read(f, len(back), io)
        assert st == 0 and d
        back += d
    t2 = time.time()
    n.remove(root, name)
    print('%d KB in %d-byte requests: write %.0f KB/s, read %.0f KB/s, data %s' % (
        size // 1024, io, size / 1024 / (t1 - t0), size / 1024 / (t2 - t1),
        'identical' if back == data else 'DIFFERENT'))
    return 0 if back == data else 1


if __name__ == '__main__':
    sys.exit(main())
