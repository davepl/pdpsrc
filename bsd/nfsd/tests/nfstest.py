#!/usr/bin/env python3
"""Protocol test for nfsd: speaks ONC RPC / NFSv3 directly, no mount needed.

    nfstest.py [-p nfsport] [-P pmapport] [-u uid] [-g gid] [-q] host /export

The export must be writable by the uid given.  Everything is done inside a
scratch directory created under the export and removed at the end.
"""
import os, random, socket, struct, sys, time, getopt

PMAP, NFS, MNT = 100000, 100003, 100005
OK, PERM, NOENT, ACCES, EXIST, NOTDIR, ISDIR, INVAL = 0, 1, 2, 13, 17, 20, 21, 22
ROFS, NAMETOOLONG, NOTEMPTY, STALE = 30, 63, 66, 70
BADHANDLE, NOTSUPP = 10001, 10004


class Pack:
    def __init__(self):
        self.b = bytearray()

    def u32(self, v):
        self.b += struct.pack('>I', v & 0xffffffff)
        return self

    def u64(self, v):
        self.b += struct.pack('>Q', v)
        return self

    def opaque(self, d):
        if isinstance(d, str):
            d = d.encode()
        self.u32(len(d))
        self.b += d + b'\0' * ((4 - len(d) % 4) % 4)
        return self

    def fixed(self, d):
        self.b += d + b'\0' * ((4 - len(d) % 4) % 4)
        return self


class Unpack:
    def __init__(self, b):
        self.b, self.p = b, 0

    def u32(self):
        v = struct.unpack_from('>I', self.b, self.p)[0]
        self.p += 4
        return v

    def u64(self):
        v = struct.unpack_from('>Q', self.b, self.p)[0]
        self.p += 8
        return v

    def opaque(self):
        n = self.u32()
        d = bytes(self.b[self.p:self.p + n])
        self.p += (n + 3) & ~3
        return d

    def fixed(self, n):
        d = bytes(self.b[self.p:self.p + n])
        self.p += (n + 3) & ~3
        return d

    def done(self):
        return self.p == len(self.b)


class RpcError(Exception):
    pass


class Rpc:
    def __init__(self, host, port, prog, vers, uid=0, gid=0, udp=False):
        self.prog, self.vers, self.uid, self.gid = prog, vers, uid, gid
        self.udp = udp
        self.xid = random.randrange(1 << 31)
        if udp:
            self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.s.connect((host, port))
        else:
            self.s = socket.create_connection((host, port), timeout=60)
            # as kernel NFS clients do: never hold a request back
            self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.s.settimeout(60)

    def close(self):
        self.s.close()

    def header(self, proc, vers=None):
        self.xid += 1
        p = Pack()
        p.u32(self.xid).u32(0).u32(2).u32(self.prog)
        p.u32(self.vers if vers is None else vers).u32(proc)
        c = Pack().u32(0).opaque('nfstest').u32(self.uid).u32(self.gid).u32(0)
        p.u32(1).opaque(bytes(c.b))
        p.u32(0).u32(0)
        return p

    def send(self, proc, args=b'', vers=None):
        msg = bytes(self.header(proc, vers).b) + bytes(args)
        if self.udp:
            self.s.send(msg)
        else:
            self.s.sendall(struct.pack('>I', 0x80000000 | len(msg)) + msg)
        return self.xid

    def recv(self):
        if self.udp:
            data = self.s.recv(65536)
        else:
            data = b''
            last = False
            while not last:
                h = self._rd(4)
                m = struct.unpack('>I', h)[0]
                last = bool(m & 0x80000000)
                data += self._rd(m & 0x7fffffff)
        u = Unpack(data)
        xid = u.u32()
        if u.u32() != 1:
            raise RpcError('not a reply')
        if u.u32() != 0:
            raise RpcError('call denied')
        u.u32()
        u.opaque()
        st = u.u32()
        if st != 0:
            e = RpcError('accept_stat %d' % st)
            e.stat = st
            if st == 2:
                e.range = (u.u32(), u.u32())
            raise e
        return xid, u

    def _rd(self, n):
        d = b''
        while len(d) < n:
            c = self.s.recv(n - len(d))
            if not c:
                raise RpcError('connection closed')
            d += c
        return d

    def call(self, proc, args=b'', vers=None):
        x = self.send(proc, args, vers)
        xid, u = self.recv()
        if xid != x:
            raise RpcError('xid mismatch')
        return u


def fattr(u):
    a = {}
    a['type'], a['mode'], a['nlink'], a['uid'], a['gid'] = [u.u32() for _ in range(5)]
    a['size'], a['used'] = u.u64(), u.u64()
    a['rdev'] = (u.u32(), u.u32())
    a['fsid'], a['fileid'] = u.u64(), u.u64()
    a['atime'], _, a['mtime'], _, a['ctime'], _ = [u.u32() for _ in range(6)]
    return a


def postattr(u):
    return fattr(u) if u.u32() else None


def wcc(u):
    pre = None
    if u.u32():
        pre = (u.u64(), u.u32(), u.u32(), u.u32(), u.u32())
    return pre, postattr(u)


def sattr(mode=None, uid=None, gid=None, size=None, atime=None, mtime=None):
    p = Pack()
    for v in (mode, uid, gid):
        if v is None:
            p.u32(0)
        else:
            p.u32(1).u32(v)
    if size is None:
        p.u32(0)
    else:
        p.u32(1).u64(size)
    for v in (atime, mtime):
        if v is None:
            p.u32(0)
        elif v == 'now':
            p.u32(1)
        else:
            p.u32(2).u32(v).u32(0)
    return bytes(p.b)


class Nfs:
    """Thin wrappers: each returns (status, ...)."""

    def __init__(self, rpc):
        self.r = rpc

    def getattr(self, fh):
        u = self.r.call(1, Pack().opaque(fh).b)
        st = u.u32()
        return (st, fattr(u)) if st == 0 else (st, None)

    def setattr(self, fh, guard=None, **kw):
        p = Pack().opaque(fh)
        p.b += sattr(**kw)
        if guard is None:
            p.u32(0)
        else:
            p.u32(1).u32(guard).u32(0)
        u = self.r.call(2, p.b)
        st = u.u32()
        return st, wcc(u)

    def lookup(self, d, name):
        u = self.r.call(3, Pack().opaque(d).opaque(name).b)
        st = u.u32()
        if st:
            return st, None, None
        fh = u.opaque()
        return st, fh, postattr(u)

    def access(self, fh, mask=0x3f):
        u = self.r.call(4, Pack().opaque(fh).u32(mask).b)
        st = u.u32()
        postattr(u)
        return st, (u.u32() if st == 0 else 0)

    def readlink(self, fh):
        u = self.r.call(5, Pack().opaque(fh).b)
        st = u.u32()
        postattr(u)
        return st, (u.opaque() if st == 0 else None)

    def read(self, fh, off, cnt):
        u = self.r.call(6, Pack().opaque(fh).u64(off).u32(cnt).b)
        st = u.u32()
        a = postattr(u)
        if st:
            return st, None, None
        n, eof = u.u32(), u.u32()
        d = u.opaque()
        assert len(d) == n and u.done()
        return st, d, eof

    def write(self, fh, off, data, stable=0):
        p = Pack().opaque(fh).u64(off).u32(len(data)).u32(stable).opaque(data)
        u = self.r.call(7, p.b)
        st = u.u32()
        w = wcc(u)
        if st:
            return st, 0, None
        n, committed, verf = u.u32(), u.u32(), u.fixed(8)
        return st, n, verf

    def _newobj(self, u):
        st = u.u32()
        if st:
            return st, None, None
        fh = u.opaque() if u.u32() else None
        a = postattr(u)
        wcc(u)
        return st, fh, a

    def create(self, d, name, how=0, verf=b'', **kw):
        p = Pack().opaque(d).opaque(name).u32(how)
        if how == 2:
            p.fixed(verf)
        else:
            p.b += sattr(**kw)
        return self._newobj(self.r.call(8, p.b))

    def mkdir(self, d, name, mode=0o755):
        p = Pack().opaque(d).opaque(name)
        p.b += sattr(mode=mode)
        return self._newobj(self.r.call(9, p.b))

    def symlink(self, d, name, target):
        p = Pack().opaque(d).opaque(name)
        p.b += sattr(mode=0o777)
        p.opaque(target)
        return self._newobj(self.r.call(10, p.b))

    def remove(self, d, name):
        return self.r.call(12, Pack().opaque(d).opaque(name).b).u32()

    def rmdir(self, d, name):
        return self.r.call(13, Pack().opaque(d).opaque(name).b).u32()

    def rename(self, d1, n1, d2, n2):
        p = Pack().opaque(d1).opaque(n1).opaque(d2).opaque(n2)
        return self.r.call(14, p.b).u32()

    def link(self, fh, d, name):
        return self.r.call(15, Pack().opaque(fh).opaque(d).opaque(name).b).u32()

    def readdir(self, d, count=1024, plus=False):
        """All entries: list of (name, fileid[, attrs, fh])."""
        out, cookie, calls = [], 0, 0
        while True:
            p = Pack().opaque(d).u64(cookie).fixed(b'\0' * 8)
            if plus:
                p.u32(count)
            p.u32(count)
            u = self.r.call(17 if plus else 16, p.b)
            calls += 1
            st = u.u32()
            postattr(u)
            if st:
                return st, out, calls
            u.fixed(8)
            n = 0
            while u.u32():
                fileid, name, cookie = u.u64(), u.opaque().decode(), u.u64()
                if plus:
                    a = postattr(u)
                    fh = u.opaque() if u.u32() else None
                    out.append((name, fileid, a, fh))
                else:
                    out.append((name, fileid))
                n += 1
            eof = u.u32()
            assert u.done(), 'trailing bytes in readdir reply'
            if eof:
                return 0, out, calls
            assert n, 'empty readdir reply without eof'

    def fsstat(self, fh):
        u = self.r.call(18, Pack().opaque(fh).b)
        st = u.u32()
        postattr(u)
        return st, ([u.u64() for _ in range(6)] if st == 0 else None)

    def fsinfo(self, fh):
        u = self.r.call(19, Pack().opaque(fh).b)
        st = u.u32()
        postattr(u)
        return st, ([u.u32() for _ in range(7)] if st == 0 else None)

    def pathconf(self, fh):
        u = self.r.call(20, Pack().opaque(fh).b)
        st = u.u32()
        postattr(u)
        return st, ([u.u32() for _ in range(6)] if st == 0 else None)

    def commit(self, fh):
        u = self.r.call(21, Pack().opaque(fh).u64(0).u32(0).b)
        st = u.u32()
        wcc(u)
        return st, (u.fixed(8) if st == 0 else None)


passed = failed = 0
quiet = False


def check(what, cond, detail=''):
    global passed, failed
    if cond:
        passed += 1
        if not quiet:
            print('ok    ' + what)
    else:
        failed += 1
        print('FAIL  ' + what + (' : ' + str(detail) if detail != '' else ''))
    return cond


def main():
    global quiet
    opts, args = getopt.getopt(sys.argv[1:], 'p:P:u:g:qs:')
    o = dict(opts)
    if len(args) != 2:
        print(__doc__)
        return 2
    host, export = args
    uid, gid = int(o.get('-u', os.getuid())), int(o.get('-g', os.getgid()))
    pmapport = int(o.get('-P', 111))
    quiet = '-q' in o
    seed = int(o.get('-s', time.time()))
    random.seed(seed)
    t0 = time.time()

    # --- portmapper, over TCP and UDP
    for udp in (False, True):
        pm = Rpc(host, pmapport, PMAP, 2, udp=udp)
        kind = 'udp' if udp else 'tcp'
        pm.call(0)
        port = pm.call(3, Pack().u32(NFS).u32(3).u32(6).u32(0).b).u32()
        check('portmap %s: NFS v3 tcp port' % kind, port != 0, port)
        mport = pm.call(3, Pack().u32(MNT).u32(3).u32(6).u32(0).b).u32()
        check('portmap %s: MOUNT v3 tcp port' % kind, mport != 0, mport)
        check('portmap %s: unknown program is port 0' % kind,
              pm.call(3, Pack().u32(100021).u32(4).u32(6).u32(0).b).u32() == 0)
        u = pm.call(4)
        n = 0
        while u.u32():
            u.u32(), u.u32(), u.u32(), u.u32()
            n += 1
        check('portmap %s: dump lists the programs' % kind, n >= 3, n)
        try:
            pm.call(0, vers=4)
            check('portmap %s: version 4 refused' % kind, False)
        except RpcError as e:
            check('portmap %s: version 4 refused with the range' % kind,
                  getattr(e, 'stat', 0) == 2 and e.range == (2, 2), e)
        pm.close()
    if '-p' in o:
        port = mport = int(o['-p'])

    # --- MOUNT
    for udp in (False, True):
        kind = 'udp' if udp else 'tcp'
        mn = Rpc(host, mport, MNT, 3, uid, gid, udp=udp)
        u = mn.call(5)
        exps = []
        while u.u32():
            exps.append(u.opaque().decode())
            while u.u32():
                u.opaque()
        # the path under test may be a subdirectory of an exported one
        check('mount %s: export list covers %s' % (kind, export),
              any(export == e or e == '/' or export.startswith(e + '/')
                  for e in exps), exps)
        u = mn.call(1, Pack().opaque(export).b)
        st = u.u32()
        check('mount %s: MNT %s' % (kind, export), st == 0, st)
        if st:
            return 1
        root = u.opaque()
        flavors = [u.u32() for _ in range(u.u32())]
        check('mount %s: AUTH_SYS offered' % kind, 1 in flavors, flavors)
        u = mn.call(1, Pack().opaque('/no/such/export').b)
        check('mount %s: unknown path refused' % kind, u.u32() != 0)
        mn.call(3, Pack().opaque(export).b)
        mn.close()

    rpc = Rpc(host, port, NFS, 3, uid, gid)
    n = Nfs(rpc)
    try:
        rpc.call(0, vers=4)
        check('nfs: version 4 refused', False)
    except RpcError as e:
        check('nfs: version 4 refused with the range',
              getattr(e, 'stat', 0) == 2 and e.range == (3, 3), e)
    rpc.call(0)

    st, a = n.getattr(root)
    check('getattr root is a directory', st == 0 and a['type'] == 2, (st, a))
    st, info = n.fsinfo(root)
    check('fsinfo', st == 0 and info[0] >= 1024, (st, info))
    iosize = min(info[0], info[3]) if st == 0 else 4096
    st, fs = n.fsstat(root)
    check('fsstat', st == 0 and fs[0] > 0 and fs[1] <= fs[0], (st, fs))
    st, pc = n.pathconf(root)
    check('pathconf', st == 0 and pc[1] >= 14, (st, pc))
    namemax = pc[1] if st == 0 else 63
    st, acc = n.access(root)
    check('access root', st == 0 and (acc & 3) == 3, (st, acc))
    check('getattr on a made-up handle fails',
          n.getattr(b'bogus-handle')[0] in (BADHANDLE, STALE))
    forged = bytearray(root)
    forged[-1] ^= 0x55
    check('getattr on a handle for another directory is stale',
          n.getattr(bytes(forged))[0] in (STALE, BADHANDLE))

    # --- scratch directory
    tname = 'nfstest.%d' % os.getpid()
    st, top, a = n.mkdir(root, tname)
    if not check('mkdir scratch directory', st == 0 and top, st):
        return 1
    check('mkdir again says it exists', n.mkdir(root, tname)[0] == EXIST)
    st, fh2, a = n.lookup(root, tname)
    check('lookup finds it with the same handle', st == 0 and fh2 == top)
    check('lookup . is the directory itself', n.lookup(top, '.')[1] == top)
    check('lookup .. is the parent', n.lookup(top, '..')[1] == root)
    check('lookup of a missing name', n.lookup(top, 'nope')[0] == NOENT)
    check('lookup of a name with a slash', n.lookup(top, 'a/b')[0] != 0)
    check('lookup of a name that is too long',
          n.lookup(top, 'x' * (namemax + 1))[0] in (NAMETOOLONG, NOENT))

    # --- create, write, read
    st, f, a = n.create(top, 'file1', mode=0o644)
    check('create file1', st == 0 and f and a['size'] == 0 and a['mode'] == 0o644, (st, a))
    check('guarded create of an existing file', n.create(top, 'file1', how=1, mode=0o644)[0] == EXIST)
    check('unchecked create of an existing file', n.create(top, 'file1', mode=0o600)[0] == 0)
    data = bytes(random.randrange(256) for _ in range(3 * iosize + 1234))
    off = 0
    okw = True
    verfs = set()
    while off < len(data):
        st, cnt, verf = n.write(f, off, data[off:off + iosize])
        if st or cnt <= 0:
            okw = False
            break
        verfs.add(verf)
        off += cnt
    check('write %d bytes in %d-byte requests' % (len(data), iosize), okw, st)
    st, verf = n.commit(f)
    verfs.add(verf)
    check('commit, with one write verifier throughout', st == 0 and len(verfs) == 1, verfs)
    st, a = n.getattr(f)
    check('size after writing', st == 0 and a['size'] == len(data), a and a['size'])
    back = b''
    eof = 0
    while not eof:
        st, d, eof = n.read(f, len(back), iosize)
        if st or (not d and not eof):
            break
        back += d
    check('read it back identical', back == data, (len(back), len(data)))
    st, d, eof = n.read(f, len(data) + 10, 100)
    check('read past the end is empty with eof', st == 0 and d == b'' and eof)
    st, d, eof = n.read(f, 100, 50)
    check('short read in the middle', st == 0 and d == data[100:150] and not eof)
    # scattered rewrites
    buf = bytearray(data)
    for _ in range(12):
        o2 = random.randrange(len(buf))
        ln = random.randrange(1, 3000)
        chunk = bytes(random.randrange(256) for _ in range(ln))
        st, cnt, _ = n.write(f, o2, chunk, stable=2)
        buf[o2:o2 + ln] = chunk
    back = b''
    eof = 0
    while not eof:
        st, d, eof = n.read(f, len(back), iosize)
        if st or (not d and not eof):
            break
        back += d
    check('scattered overwrites and extension read back', back == bytes(buf), (len(back), len(buf)))
    # pipelined requests on one connection
    xids = [rpc.send(6, Pack().opaque(f).u64(i * 512).u32(512).b) for i in range(24)]
    good = True
    for i, x in enumerate(xids):
        xid, u = rpc.recv()
        st = u.u32()
        postattr(u)
        u.u32(), u.u32()
        if xid != x or st or u.opaque() != bytes(buf[i * 512:i * 512 + 512]):
            good = False
    check('24 pipelined reads answered in order', good)

    # --- setattr
    st, w = n.setattr(f, mode=0o600)
    check('setattr mode', st == 0 and w[1]['mode'] == 0o600, (st, w))
    st, w = n.setattr(f, size=1000)
    check('setattr truncates', st == 0 and w[1]['size'] == 1000, (st, w))
    st, d, eof = n.read(f, 0, iosize)
    check('truncated contents', d == bytes(buf[:1000]) and eof)
    st, w = n.setattr(f, size=5000)
    st, d, eof = n.read(f, 0, iosize)
    check('extension by setattr reads as zeros', d == bytes(buf[:1000]) + b'\0' * 4000, len(d or b''))
    st, w = n.setattr(f, atime=1000000000, mtime=1100000000)
    check('setattr times', st == 0 and w[1]['mtime'] == 1100000000 and w[1]['atime'] == 1000000000, (st, w))
    st, w = n.setattr(f, guard=12345, mode=0o644)
    check('guarded setattr with the wrong ctime', st == 10002, st)
    st, a = n.getattr(f)
    st, w = n.setattr(f, guard=a['ctime'], mode=0o644)
    check('guarded setattr with the right ctime', st == 0 and w[1]['mode'] == 0o644, st)

    # --- a file created read-only can still be written by its creator
    st, ro, a = n.create(top, 'readonly', mode=0o444)
    st, cnt, _ = n.write(ro, 0, b'written after a 0444 create')
    check('write to a file just created with mode 0444', st == 0 and cnt == 27, st)

    # --- exclusive create
    v1, v2 = b'ABCDEFGH', b'12345678'
    st, x, a = n.create(top, 'excl', how=2, verf=v1)
    check('exclusive create', st == 0, st)
    check('exclusive create repeated with the same verifier', n.create(top, 'excl', how=2, verf=v1)[0] == 0)
    check('exclusive create with another verifier', n.create(top, 'excl', how=2, verf=v2)[0] == EXIST)

    # --- symlink, link
    st, sl, a = n.symlink(top, 'sym', 'file1')
    check('symlink', st == 0 and a and a['type'] == 5, (st, a))
    st, tgt = n.readlink(sl)
    check('readlink', st == 0 and tgt == b'file1', tgt)
    check('readlink of a regular file', n.readlink(f)[0] == INVAL)
    check('lookup through a symlink handle is refused', n.lookup(sl, 'x')[0] == NOTDIR)
    st = n.link(f, top, 'hard')
    st2, a = n.getattr(f)
    check('link', st == 0 and a['nlink'] == 2, (st, a and a['nlink']))
    st, h, ha = n.lookup(top, 'hard')
    check('the link is the same file', st == 0 and ha['fileid'] == a['fileid'])

    # --- rename
    st, sub, a = n.mkdir(top, 'sub')
    check('rename within a directory', n.rename(top, 'file1', top, 'file2') == 0)
    check('old name is gone', n.lookup(top, 'file1')[0] == NOENT)
    st, a = n.getattr(f)
    check('handle still good after the rename', st == 0 and a['size'] == 5000, st)
    check('rename into another directory', n.rename(top, 'file2', sub, 'moved') == 0)
    st, d, eof = n.read(f, 0, 100)
    check('handle still good after moving', st == 0 and d == bytes(buf[:100]), st)
    st, m, a = n.lookup(sub, 'moved')
    check('lookup under the new name', st == 0 and a['size'] == 5000, st)
    check('rename over an existing file', n.rename(top, 'readonly', top, 'excl') == 0)
    st, a = n.getattr(ro)
    check('the renamed file keeps its handle', st == 0 and a['size'] == 27, st)
    check('the replaced file is stale', n.getattr(x)[0] == STALE)
    check('rename a directory', n.rename(top, 'sub', top, 'sub2') == 0)
    st, a = n.getattr(m)
    check('file below a renamed directory', st == 0 and a['size'] == 5000, st)
    check('rename of a missing file', n.rename(top, 'nope', top, 'nope2') == NOENT)

    # --- readdir
    names = ['f%03d' % i for i in range(70)] + ['a-rather-longer-name-%02d.txt' % i for i in range(10)]
    hs = {}
    for nm in names:
        st, h, a = n.create(top, nm, mode=0o644)
        hs[nm] = h
        if st:
            check('create ' + nm, False, st)
            break
    want = set(names) | {'.', '..', 'excl', 'sym', 'hard', 'sub2'}
    for cnt in (300, 1024, 8192):
        st, ents, calls = n.readdir(top, cnt)
        got = [e[0] for e in ents]
        check('readdir count=%d: every name once (%d calls)' % (cnt, calls),
              st == 0 and set(got) == want and len(got) == len(want),
              (st, sorted(set(got) ^ want)[:6], len(got), len(want)))
    st, ents, calls = n.readdir(top, 4096, plus=True)
    if st == NOTSUPP:
        check('readdirplus not supported (client falls back)', True)
    else:
        got = [e[0] for e in ents]
        check('readdirplus: every name once (%d calls)' % calls,
              st == 0 and set(got) == want and len(got) == len(want), (st, len(got), len(want)))
        byname = {e[0]: e for e in ents}
        e = byname.get('f000')
        check('readdirplus: handle and attributes match lookup',
              e and e[3] == hs['f000'] and e[2] and e[2]['type'] == 1, e)
        e = byname.get('sym')
        check('readdirplus: symlink type', e and e[2] and e[2]['type'] == 5)

    # --- handles that are no longer cached are worked out again
    st, a = n.getattr(hs['f000'])
    check('handle evicted from the cache still resolves', st == 0 and a['type'] == 1, st)
    st, a = n.getattr(m)
    check('handle of a moved file', st in (0, STALE), st)

    # --- deep trees
    d, depth = top, 0
    chain = [top]
    while depth < 40:
        st, nd, a = n.mkdir(d, 'd')
        if st or nd is None:
            break
        d = nd
        chain.append(d)
        depth += 1
    check('nested directories (%d levels, then %d)' % (depth, st), depth >= 8 and st in (0, NAMETOOLONG), st)
    st, fd, a = n.create(chain[-1], 'deepfile', mode=0o644)
    if st == 0:
        check('write in the deepest directory', n.write(fd, 0, b'deep')[0] == 0)
    for i in range(len(chain) - 1, 0, -1):
        n.remove(chain[i], 'deepfile')
        st = n.rmdir(chain[i - 1], 'd')
        if st:
            check('rmdir nested level %d' % i, False, st)
            break
    else:
        check('nested directories removed', True)

    # --- remove
    check('rmdir of a non-empty directory', n.rmdir(top, 'sub2') in (NOTEMPTY, EXIST))
    check('remove of a directory', n.remove(top, 'sub2') in (ISDIR, PERM))
    check('rmdir of a file', n.rmdir(top, 'hard') == NOTDIR)
    check('remove of a missing file', n.remove(top, 'nope') == NOENT)
    st, sub2, a = n.lookup(top, 'sub2')
    check('remove moved', n.remove(sub2, 'moved') == 0)
    check('rmdir sub2', n.rmdir(top, 'sub2') == 0)
    check('removed directory handle is stale', n.getattr(sub2)[0] == STALE)
    for nm in names + ['excl', 'sym', 'hard']:
        st = n.remove(top, nm)
        if st:
            check('remove ' + nm, False, st)
    st, ents, calls = n.readdir(top, 4096)
    check('scratch directory is empty again', st == 0 and sorted(e[0] for e in ents) == ['.', '..'],
          [e[0] for e in ents])
    check('rmdir scratch directory', n.rmdir(root, tname) == 0)
    check('its handle is stale', n.getattr(top)[0] == STALE)
    st, a = n.getattr(root)
    check('root still fine', st == 0)
    rpc.close()

    print('%d passed, %d failed, %.1f s (seed %d)' % (passed, failed, time.time() - t0, seed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
