#!/bin/sh
# Generated manifest from tools/split_libc.py against Minerva libc.a.
# Invoke from bsd/links/upstream. Needs native ar/ranlib, not Python.
# Do not install over /lib/libc.a. Custom csv.o and crt0 profiling stubs
# are supplied separately by the browser link, so csv.o/mon.o stay excluded.
# Old 2.11BSD sh exits even for false conditional tests under set -e.
if test ! -d ../native-tmp/libc-root; then mkdir ../native-tmp/libc-root || exit 1; fi
cd ../native-tmp/libc-root || exit 1
all_unsafe="_setjmp.o abs.o adjtime.o brk.o chflags.o chmod.o chown.o chroot.o \
  chdir.o dup.o execle.o execve.o execv.o fabs.o fchdir.o fchflags.o \
  fchmod.o fchown.o fetchi.o ffs.o fork.o fperr.o frexp.o fstatfs.o \
  fsync.o ftruncate.o getgroups.o getitimer.o getfsstat.o getpeername.o getpgrp.o getppid.o \
  getrlimit.o getsockopt.o insque.o kill.o killpg.o ldfps.o link.o lock.o \
  lstat.o mkdir.o mknod.o mount.o getpriority.o alrem.o phys.o dup2.o \
  pipe.o profil.o pselect.o ptrace.o quota.o bind.o flock.o aldiv.o \
  readlink.o readv.o reboot.o recvfrom.o recvmsg.o remque.o rename.o accept.o \
  bcmp.o getsockname.o listen.o rmdir.o _getlogin.o sendmsg.o setgid.o setgroups.o \
  bzero.o recv.o htons.o htonl.o sendto.o setjmp.o rollback.o setlogin.o \
  setpgrp.o setpriority.o setquota.o getegid.o getgid.o setegid.o setrlimit.o geteuid.o \
  getuid.o seteuid.o setsockopt.o settimeofday.o setuid.o shutdown.o sigaltstack.o sigpending.o \
  sigstack.o sigsuspend.o sigwait.o socketpair.o statfs.o strncat.o symlink.o sync.o \
  connect.o access.o send.o socket.o bcopy.o execl.o sigaction.o sigreturn.o \
  strlen.o gettimeofday.o getrusage.o strncpy.o fcntl.o getdtablesize.o sigprocmask.o getpid.o \
  truncate.o index.o rindex.o strcmp.o strncmp.o fstat.o open.o stat.o \
  strcat.o lmul.o setitimer.o ucall.o ulsh.o umask.o umount.o __sysctl.o \
  strcpy.o unlink.o ldiv.o lrem.o select.o utimes.o vfork.o vhangup.o \
  ldexp.o almul.o udiv.o wait4.o writev.o modf.o alloca.o \
  uldiv.o ulrem.o ultof.o ioctl.o close.o lseek.o csv.o read.o \
  sbrk.o write.o _exit.o mon.o"
root_members="_setjmp.o abs.o adjtime.o brk.o chflags.o chmod.o chown.o chroot.o \
  chdir.o dup.o execle.o execve.o execv.o fabs.o fchdir.o fchflags.o \
  fchmod.o fchown.o fetchi.o ffs.o fork.o fperr.o frexp.o fstatfs.o \
  fsync.o ftruncate.o getgroups.o getitimer.o getfsstat.o getpeername.o getpgrp.o getppid.o \
  getrlimit.o getsockopt.o insque.o kill.o killpg.o ldfps.o link.o lock.o \
  lstat.o mkdir.o mknod.o mount.o getpriority.o alrem.o phys.o dup2.o \
  pipe.o profil.o pselect.o ptrace.o quota.o bind.o flock.o aldiv.o \
  readlink.o readv.o reboot.o recvfrom.o recvmsg.o remque.o rename.o accept.o \
  bcmp.o getsockname.o listen.o rmdir.o _getlogin.o sendmsg.o setgid.o setgroups.o \
  bzero.o recv.o htons.o htonl.o sendto.o setjmp.o rollback.o setlogin.o \
  setpgrp.o setpriority.o setquota.o getegid.o getgid.o setegid.o setrlimit.o geteuid.o \
  getuid.o seteuid.o setsockopt.o settimeofday.o setuid.o shutdown.o sigaltstack.o sigpending.o \
  sigstack.o sigsuspend.o sigwait.o socketpair.o statfs.o strncat.o symlink.o sync.o \
  connect.o access.o send.o socket.o bcopy.o execl.o sigaction.o sigreturn.o \
  strlen.o gettimeofday.o getrusage.o strncpy.o fcntl.o getdtablesize.o sigprocmask.o getpid.o \
  truncate.o index.o rindex.o strcmp.o strncmp.o fstat.o open.o stat.o \
  strcat.o lmul.o setitimer.o ucall.o ulsh.o umask.o umount.o __sysctl.o \
  strcpy.o unlink.o ldiv.o lrem.o select.o utimes.o vfork.o vhangup.o \
  ldexp.o almul.o udiv.o wait4.o writev.o modf.o alloca.o \
  uldiv.o ulrem.o ultof.o ioctl.o close.o lseek.o read.o sbrk.o \
  write.o _exit.o"
# Some 2.11BSD libc builds omit the floating-point common object.
ar t /lib/libc.a > ../libc-members || exit 1
if grep '^pfcom[.]o$' ../libc-members > /dev/null; then
    root_members="$root_members pfcom.o"
    all_unsafe="$all_unsafe pfcom.o"
fi
# Extract every audited non-csv/non-mon assembly member. This is deliberately
# conservative so future browser source changes do not omit a new dependency.
ar x /lib/libc.a $root_members || exit 1
rm -f ../libc-root.a
ar cr ../libc-root.a $root_members || exit 1
ranlib ../libc-root.a || exit 1
cp /lib/libc.a ../libc-safe.a || exit 1
ar d ../libc-safe.a $all_unsafe || exit 1
ranlib ../libc-safe.a || exit 1
# Use this list before the first -Z. Put libc-safe.a after browser overlays.
: > ../libc-root.list
for member in $root_members; do
    echo "../native-tmp/libc-root/$member" >> ../libc-root.list
done
echo 'Prepared private libc archive and root object list.'

# Compact argument list for the 2.11BSD exec argument-space limit.
ld -r -o ../libc-root.o $root_members || exit 1
