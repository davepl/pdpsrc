# HTTP being disabled by inetd

On September 20, the `.26` host repeatedly stopped listening on port 80 while
the console, FTP, and telnet remained usable. `/usr/adm/daemonlog` recorded:

```
http/tcp server failing (looping), service terminated
```

This message is the service invocation-rate guard, not proof of a crashing
HTTP process. Requests immediately before shutdown were successful, and the
shutdowns recurred less than a minute after restarting inetd. Each viewer's
five-second TOP poll was being forwarded through Varnish to a new HTTP process.

The [archived 2.11BSD inetd source](https://www.retro11.de/ouxr/211bsd/usr/src/usr.sbin/inetd/inetd.c.html)
defines a default of **40 starts in 60 seconds**, then disables the service
for ten minutes. The [2.11BSD manual](https://man.freebsd.org/cgi/man.cgi?manpath=2.11+BSD&query=inetd&sektion=8)
instead documents a default of 1,000. Do not assume the manual's default
matches the installed executable. The installed binary advertises `-R rate`;
using an explicit value avoids that discrepancy.

## Applied startup configuration

The existing `/etc/rc` command now launches `/usr/sbin/inetd -R 1000`.
This sets a per-service invocation-rate limit of 1,000 per minute; it does
not create 1,000 processes at once or remove the guard. The option applies
to all services managed by that inetd. No kernel or executable was replaced.

The original startup file remains on the PDP at
`/etc/rc.before-webtop-rate-20260921`. Only the inetd command was changed;
the surrounding startup sequence and file permissions (root, mode 644) were
preserved. `backup.sh` now includes `/etc/rc` in private runtime backups.

On restoration, merge `config/rc.inetd` into the existing startup command.
Do not replace a different machine's whole `/etc/rc` with this host's file.
The native website installer deliberately does not edit system startup files.

Changing the startup file does not alter an already-running process. After
checking `ps -ax` for its current PID, terminate that inetd and start
`/usr/sbin/inetd -R 1000`. Do not reuse a PID from this document. SIGHUP
reloads service definitions, but does not apply a different command-line rate.

## Reduce traffic as well

The Varnish configuration now shares the exact public `/cgi-bin/webtop`
response for five seconds, rather than forwarding every viewer's poll.
The native sampler's own cache only saves sampling work; by itself it cannot
prevent inetd from starting an HTTP process for each request.

Visitor increments remain uncached. The proxy still allows only two concurrent
backend connections. Requests that go directly to the PDP bypass these proxy
protections; this change does not establish that the old network stack can
handle arbitrary public load.

During diagnosis there was also a brief interval when ping, FTP, and telnet
were unreachable from both the Mac and caddy. Connectivity recovered without
an uptime reset; the operator confirmed the console remained responsive and
could ping the router. After recovery, `netstat -m` showed 517 cumulative
memory-allocation denials, 153/170 mbufs in use, and 73 protocol-control-block
mbufs. `netstat -i` showed no input/output errors on qe0. Buffer exhaustion is
therefore a plausible additional cause, distinct from inetd disabling HTTP.
Sharing TOP responses also reduces the TCP-connection churn that consumes
these buffers. Check whether the denied-allocation count rises, rather than
treating an old nonzero cumulative count as proof of an ongoing failure.

Socket inspection also showed Internet clients reaching the PDP directly,
bypassing Varnish. The UDM Pro's port-80 forward was subsequently corrected
from `.26` to caddy at `.45`. The DynDNS hostname was added to caddy and Varnish
so that address also works through the proxy. Existing connections may outlive
a forwarding-rule change; successful cached homepage delivery alone does not
prove the PDP has recovered. Confirm fresh TOP timestamps and LAN connectivity.
