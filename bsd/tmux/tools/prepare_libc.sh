#!/bin/sh
# Assembly-entry manifest copied from the Links port's Minerva libc audit.
# Invoke from bsd/tmux/upstream. Uses native ar/ranlib, not Python.
# All outputs stay in ../native-tmp; /lib/libc.a is read-only input.
# tmux uses installed crt0/csv, so retain csv.o in the root archive.
# Archive extraction remains selective; do not force-load every root member.
# Re-audit this manifest if Minerva's libc changes.
# No set -e: this native shell mishandles false conditional tests under -e.
if test ! -d ../native-tmp; then mkdir ../native-tmp || exit 1; fi
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
  ldexp.o almul.o udiv.o wait4.o writev.o pfcom.o modf.o alloca.o \
  uldiv.o ulrem.o ultof.o ioctl.o close.o lseek.o csv.o read.o \
  sbrk.o write.o _exit.o mon.o"
root_members="$all_unsafe"
ar x /lib/libc.a $root_members || exit 1
rm -f ../libc-root.a
ar cr ../libc-root.a $root_members || exit 1
ranlib ../libc-root.a || exit 1
cp /lib/libc.a ../libc-safe.a || exit 1
ar d ../libc-safe.a $all_unsafe || exit 1
ranlib ../libc-safe.a || exit 1
: > ../libc-root.list
for member in $root_members; do
    echo "../native-tmp/libc-root/$member" >> ../libc-root.list
done
echo 'Prepared private libc archives; link safe C entries in an overlay and root assembly selectively.'
