/* Native wire/FILETIME checks with fixed independent wire byte vectors. */
#include "../smbd.h"
static int failures;
static void check(got, want, label)
const u8 *got, *want;
const char *label;
{
 if (memcmp(got, want, 8)) { printf("FAIL %s\n", label); failures++; }
}
int main()
{
 u8 p[8];
 static u8 integer[8] = {0x78,0x56,0x34,0x12,0xef,0xcd,0xab,0x90};
 static u8 time0[8] = {0x00,0x80,0x3e,0xd5,0xde,0xb1,0x9d,0x01};
 static u8 time1[8] = {0x80,0x16,0xd7,0xd5,0xde,0xb1,0x9d,0x01};
 static u8 time2[8] = {0x00,0x00,0x6d,0xc6,0x47,0x17,0xda,0x01};
 static u8 time3[8] = {0x80,0xe9,0xa5,0xd4,0x1e,0xfd,0xe9,0x01};
 static u8 time4[8] = {0x00,0x80,0x3e,0xd5,0xde,0xb1,0x9d,0x01};
 put64(p,0x12345678UL,0x90abcdefUL);
 check(p,integer,"64-bit encoding");
 if (get32(integer)!=0x12345678UL || get32(integer+4)!=0x90abcdefUL)
  failures++;
 filetime(p,0L); check(p,time0,"FILETIME 0");
 filetime(p,1L); check(p,time1,"FILETIME 1");
 filetime(p,1700000000L); check(p,time2,"FILETIME 1700000000");
 filetime(p,2147483647L); check(p,time3,"FILETIME 2147483647");
 filetime(p,-1L); check(p,time4,"FILETIME -1");
 printf("native wire/FILETIME vectors: %s\n",failures?"FAILED":"PASS");
 return failures?1:0;
}
