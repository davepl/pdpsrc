char* Version= "minirb 3.02 12-21-94 Copyright 1994 Omen Technology INC";
#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "../pdp11_unistd.h"

void wcreceive();
int wcrxpn(), procheader(), wcrx(), sendline(), wcgetsec(), rdln();

FILE *fout; long Bytesleft; int Blklen; char secbuf[1024]; char linbuf[1024];
int Lleft=0; jmp_buf tohere;

void mode(n) { if (n) system("stty raw -echo"); else system("stty echo -raw"); }

void alrm(c) { longjmp(tohere, -1); }

void bibi(n) {
  mode(0); fprintf(stderr, "minirb: signal %d; exiting", n); exit(128+n); }

main() {
 mode(1); if (signal(SIGINT, bibi) == SIG_IGN) {
  signal(SIGINT, SIG_IGN); signal(SIGKILL, SIG_IGN); } else {
  signal(SIGINT, bibi); signal(SIGKILL, bibi); }
 printf("%s\r\n\n\n", Version);
 printf("Send your files with a YAM/ZCOMM \042sb file ...\042 command\r\n");
 wcreceive(); mode(0); exit(0); }

void wcreceive() {
 for (;;) {
  if (wcrxpn(secbuf) == -1) break;
  if (secbuf[0]==0) return;
  if (procheader(secbuf)== -1 || wcrx()== -1) break; } }

wcrxpn(rpn) char *rpn; { register c;
et_tu:
 sendline(025); Lleft=0; while ((c = wcgetsec(rpn)) != 0) {
  if (c == -10) { sendline(6); Lleft=0; rdln(2); goto et_tu; } return -1; }
 sendline(6); return 0; }

wcrx() { register int sectnum, sectcurr, sendchar, cblklen;
 sectnum=0; sendchar=025;
for (;;) {
  sendline(sendchar); Lleft=0; sectcurr=wcgetsec(secbuf);
  if (sectcurr==(sectnum+1 & 0377)) {
   sectnum++; cblklen = Bytesleft>Blklen ? Blklen:Bytesleft;
   fwrite(secbuf, cblklen, 1, fout);
   if ((Bytesleft-=cblklen) < 0) Bytesleft = 0;
   sendchar=6; }
  else if (sectcurr==(sectnum&0377)) sendchar=6;
  else if (sectcurr== -10) { fclose(fout); sendline(6); Lleft=0; return 0; }
  else return -1; } }

wcgetsec(rxbuf) char *rxbuf; {
 register checksum, wcj, firstch; register char *p; int sectcurr, errors;
 for (errors=0; errors<15; errors++) {
  if ((firstch=rdln(5))==2) { Blklen=1024; goto get2; }
  if (firstch==1) { Blklen=128;
get2:
   sectcurr=rdln(2); checksum=0;
   if ((sectcurr+(rdln(2)))==0377) {
    for (p=rxbuf,wcj=Blklen; --wcj>=0; ) {
     if ((firstch=rdln(2)) < 0) goto bilge;
     checksum += (*p++ = firstch); }
    if ((firstch=rdln(2)) < 0) goto bilge;
    if (((checksum-firstch)&0377)==0) return sectcurr;
   } }
  else if (firstch==4) return -10;
  else if (firstch==24) return -1;
bilge: while(rdln(2)!= -2) ;
  sendline(025); Lleft=0; }
 return -1; }

rdln(timeout) int timeout; { static char *cdq;
 if (--Lleft >= 0) return (*cdq++ & 0377);
 if (setjmp(tohere)) { Lleft = 0; return -2; }
 signal(SIGALRM, alrm); alarm(timeout);
 Lleft=read(0, cdq=linbuf, 1024); alarm(0);
 if (Lleft < 1) return -2;
 --Lleft; return (*cdq++ & 0377); }

procheader(name) char *name; { register char *p;
 Bytesleft = 2000000000L; p = name + 1 + strlen(name);
 if (*p) sscanf(p, "%ld", &Bytesleft);
 if ((fout=fopen(name, "w")) == NULL) return -1;
 return 0; }

sendline(c) { char d; d = c; write(1, &d, 1); }

/* End of minirb.c */
