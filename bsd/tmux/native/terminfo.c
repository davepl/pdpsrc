/* Native termcap names for capabilities requested by tmux 1.1. */
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "term.h"
extern int tgetent(), tgetflag(), tgetnum();
extern char *tgetstr();
/* Scratch storage exists only while tty_term_find copies the capabilities. */
static char *entry, *value;
char *cur_term;
struct capability { char *info; char *cap; };
static struct capability names[] = {
	{ "acsc", "ac" }, { "bel", "bl" }, { "blink", "mb" },
	{ "bold", "md" }, { "civis", "vi" }, { "clear", "cl" },
	{ "cnorm", "ve" }, { "csr", "cs" }, { "cub", "LE" },
	{ "cub1", "le" }, { "cud", "DO" }, { "cud1", "do" },
	{ "cuf", "RI" }, { "cuf1", "nd" }, { "cup", "cm" },
	{ "cuu", "UP" }, { "cuu1", "up" }, { "dch", "DC" },
	{ "dch1", "dc" }, { "dim", "mh" }, { "dl", "DL" },
	{ "dl1", "dl" }, { "el", "ce" }, { "el1", "cb" },
	{ "enacs", "eA" }, { "home", "ho" }, { "hpa", "ch" },
	{ "ich", "IC" }, { "ich1", "ic" }, { "il", "AL" },
	{ "il1", "al" }, { "invis", "mk" }, { "is1", "i1" },
	{ "is2", "is" }, { "is3", "i3" }, { "kcbt", "kB" },
	{ "kcub1", "kl" }, { "kcud1", "kd" }, { "kcuf1", "kr" },
	{ "kcuu1", "ku" }, { "kdch1", "kD" }, { "kend", "@7" },
	{ "kf1", "k1" }, { "kf2", "k2" }, { "kf3", "k3" },
	{ "kf4", "k4" }, { "kf5", "k5" }, { "kf6", "k6" },
	{ "kf7", "k7" }, { "kf8", "k8" }, { "kf9", "k9" },
	{ "kf10", "k;" }, { "kf11", "F1" }, { "kf12", "F2" },
	{ "kf13", "F3" }, { "kf14", "F4" }, { "kf15", "F5" },
	{ "kf16", "F6" }, { "kf17", "F7" }, { "kf18", "F8" },
	{ "kf19", "F9" }, { "kf20", "FA" }, { "khome", "kh" },
	{ "kich1", "kI" }, { "kmous", "Km" }, { "knp", "kN" },
	{ "kpp", "kP" }, { "op", "op" }, { "rev", "mr" },
	{ "ri", "sr" }, { "rmacs", "ae" }, { "rmcup", "te" },
	{ "rmir", "ei" }, { "rmkx", "ke" }, { "setab", "AB" },
	{ "setaf", "AF" }, { "sgr0", "me" }, { "smacs", "as" },
	{ "smcup", "ti" }, { "smir", "im" }, { "smkx", "ks" },
	{ "smso", "so" }, { "smul", "us" }, { "vpa", "cv" },
	{ "colors", "Co" }, { "xenl", "xn" }, { "AX", "AX" },
	{ NULL, NULL }
};
static char *
capname(name)
char *name;
{
	struct capability *p;
	for (p = names; p->info != NULL; p++)
		if (strcmp(name, p->info) == 0) return (p->cap);
	return (NULL);
}
int
setupterm(name, fd, error)
char *name;
int fd, *error;
{
	int n;
	(void)fd;
	/* setupterm is also safe after an earlier failed or unfinished setup. */
	del_curterm(cur_term);
	entry = malloc(1024);
	value = malloc(1024);
	if (entry == NULL || value == NULL) {
		del_curterm(cur_term);
		errno = ENOMEM;
		*error = -1;
		return (ERR);
	}
	n = tgetent(entry, name);
	if (n != 1) {
		del_curterm(cur_term);
		*error = n;
		return (ERR);
	}
	if (tgetflag("hc")) {
		del_curterm(cur_term);
		*error = 1;
		return (ERR);
	}
	cur_term = entry; *error = 1;
	return (OK);
}
int
del_curterm(term)
char *term;
{
	(void)term;
	/* The original native allocator does not accept free(NULL). */
	if (entry != NULL) free(entry);
	if (value != NULL) free(value);
	entry = value = cur_term = NULL;
	return (OK);
}
char *
tigetstr(name)
char *name;
{
	char *cap, *area, *s;
	if (cur_term == NULL) return (NULL);
	if ((cap = capname(name)) == NULL) return (NULL);
	area = value;
	if ((s = tgetstr(cap, &area)) == NULL) return (NULL);
	/* Initial termcap delays are padding metadata, never literal output. */
	if (*s >= '0' && *s <= '9') {
		while (*s >= '0' && *s <= '9') s++;
		if (*s == '.') { s++; while (*s >= '0' && *s <= '9') s++; }
		if (*s == '*') s++;
	}
	return (s);
}
int
tigetnum(name)
char *name;
{
	char *cap;
	if (cur_term == NULL) return (-2);
	if ((cap = capname(name)) == NULL) return (-2);
	return (tgetnum(cap));
}
int
tigetflag(name)
char *name;
{
	char *cap;
	if (cur_term == NULL) return (-1);
	if ((cap = capname(name)) == NULL) return (-1);
	return (tgetflag(cap));
}

/* Integer terminfo override expressions and native termcap parameter syntax. */
char *
tparm(s, p1, p2, p3, p4, p5, p6, p7, p8, p9)
char *s;
int p1, p2, p3, p4, p5, p6, p7, p8, p9;
{
	static char out[128];	/* every capability tmux formats is short */
	int p[9], stack[32], vars[26], parents[16], taken[16];
	int sp, depth, active, ti, next, c, a, b, n, width;
	char number[16], *q, *end;
	p[0]=p1; p[1]=p2; p[2]=p3; p[3]=p4; p[4]=p5;
	p[5]=p6; p[6]=p7; p[7]=p8; p[8]=p9;
	memset(vars, 0, sizeof vars);
	ti = strstr(s, "%p") != NULL || strstr(s, "%{") != NULL ||
	    strstr(s, "%?") != NULL || strstr(s, "%'") != NULL;
	sp=depth=next=0; active=1; q=out; end=out+sizeof out-1;
	while ((c=(unsigned char)*s++) != 0) {
		if (c != '%') {
			if (active) { if (q==end) goto bad; *q++=c; }
			continue;
		}
		c=(unsigned char)*s++;
		if (!c) goto bad;
		if (ti && c=='?') {
			if (depth==16) goto bad;
			parents[depth]=active; taken[depth++]=0; continue;
		}
		if (ti && c=='t') {
			if (!depth) goto bad;
			a=0;
			if (active) { if (!sp) goto bad; a=stack[--sp]; }
			active=parents[depth-1] && !taken[depth-1] && a;
			if (active) taken[depth-1]=1;
			continue;
		}
		if (ti && c=='e') {
			if (!depth) goto bad;
			active=parents[depth-1] && !taken[depth-1]; continue;
		}
		if (ti && c==';') {
			if (!depth) goto bad;
			active=parents[--depth]; continue;
		}
		if (ti && (c=='p' || c=='P' || c=='g')) {
			n=(unsigned char)*s++; if (!n) goto bad;
			if (c=='p') {
				if (n<'1' || n>'9') goto bad;
				if (active) { if (sp==32) goto bad; stack[sp++]=p[n-'1']; }
			} else {
				if (n<'a' || n>'z') goto bad;
				if (active && c=='P') { if (!sp) goto bad; vars[n-'a']=stack[--sp]; }
				if (active && c=='g') { if (sp==32) goto bad; stack[sp++]=vars[n-'a']; }
			}
			continue;
		}
		if (ti && (c=='{' || c=='\'')) {
			if (c=='{') {
				n=0;
				while (*s>='0' && *s<='9') { if (n>3276) goto bad; n=n*10+*s++-'0'; }
				if (*s++!='}') goto bad;
			} else { n=(unsigned char)*s++; if (!n || *s++!='\'') goto bad; }
			if (active) { if (sp==32) goto bad; stack[sp++]=n; }
			continue;
		}
		if (!active) continue;
		if (c=='%') { if (q==end) goto bad; *q++='%'; continue; }
		if (c=='i') { p[0]++; p[1]++; continue; }
		if (!ti) {
			if (next>1) goto bad;
			if (c=='r') { a=p[0]; p[0]=p[1]; p[1]=a; continue; }
			if (c=='n') { p[0]^=0140; p[1]^=0140; continue; }
			if (c=='B') { p[next]=(p[next]/10)*16+p[next]%10; continue; }
			if (c=='D') { p[next]-=2*(p[next]%16); continue; }
			if (c=='>') {
				a=(unsigned char)*s++; if (!a) goto bad;
				b=(unsigned char)*s++; if (!b) goto bad;
				if (p[next]>a) p[next]+=b;
				continue;
			}
			if (c=='+') { a=(unsigned char)*s++; if (!a) goto bad; p[next]+=a; c='.'; }
			a=p[next++];
		} else {
			if (strchr("+-*/m&|^=><AO", c) != NULL) {
				if (sp<2) goto bad;
				b=stack[--sp]; a=stack[--sp];
				switch (c) {
				case '+': a+=b; break; case '-': a-=b; break;
				case '*': a*=b; break;
				case '/': if (!b) goto bad; a/=b; break;
				case 'm': if (!b) goto bad; a%=b; break;
				case '&': a&=b; break; case '|': a|=b; break;
				case '^': a^=b; break; case '=': a=a==b; break;
				case '>': a=a>b; break; case '<': a=a<b; break;
				case 'A': a=a&&b; break; case 'O': a=a||b; break;
				}
				stack[sp++]=a; continue;
			}
			if (!sp) goto bad;
			a=stack[--sp];
			if (c=='!' || c=='~') { stack[sp++]=c=='!' ? !a : ~a; continue; }
		}
		width=0;
		if (c==':') c=(unsigned char)*s++;
		if (c=='0') c=(unsigned char)*s++;
		if (c>='1' && c<='9') { width=c-'0'; if (ti) c=(unsigned char)*s++; else c='d'; }
		if (c=='d') {
			sprintf(number, "%0*d", width, a); n=strlen(number);
			if (n>end-q) goto bad;
			memcpy(q, number, n); q+=n;
		} else if (c=='c' || c=='.') {
			if (q==end || a==0) goto bad;
			*q++=a;
		} else goto bad;
	}
	if (depth) goto bad;
	*q=0; return (out);
bad:
	errno=EINVAL; out[0]=0; return (out);
}
