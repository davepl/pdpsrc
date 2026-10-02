#ifndef SMBD_H
#define SMBD_H
/* Native integers are never wire structures. unsigned long is >= 32 bits;
 * all arithmetic on protocol words is explicitly reduced to 32 bits. */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#if defined(pdp11) || defined(__pdp11__)
#define PDP11 1
#include <sys/time.h>
#include <sys/dir.h>
#include <sys/errno.h>
#else
#include <time.h>
#include <dirent.h>
#include <errno.h>
#endif
#include "../pdp11_unistd.h"

typedef unsigned char u8;
typedef unsigned long u32;
#define MASK32 0xffffffffUL
#define SMBD_BUFSIZE 8192
#define SMBD_PATH 512
#define SMBD_HANDLES 8
#define SMBD_COMPOUNDS 8
#define SMBD_MAXFRAME 524288L
#define SMBD_TRANSFER 65536L
#define SMBD_MAXREPLY (SMBD_COMPOUNDS * (SMBD_TRANSFER + SMBD_BUFSIZE))

#define ST_OK 0UL
#define ST_INVALID 0xc000000dUL
#define ST_DENIED 0xc0000022UL
#define ST_NOT_FOUND 0xc0000034UL
#define ST_NO_FILE 0xc000000fUL
#define ST_NOT_SUPPORTED 0xc00000bbUL
#define ST_BAD_HANDLE 0xc0000008UL
#define ST_NO_MORE 0x80000006UL
#define ST_OVERFLOW 0x80000005UL
#define ST_TOO_SMALL 0xc0000023UL
#define ST_MORE_AUTH 0xc0000016UL
#define ST_LOGON_FAILURE 0xc000006dUL
#define ST_EOF 0xc0000011UL
#define ST_NO_MEMORY 0xc0000017UL

struct hashctx { u32 h[8]; u32 lo, hi; unsigned used; u8 block[64]; };
struct hmacctx { struct hashctx inner; u8 outer[64]; int algorithm; };

struct authstate {
 int phase, authenticated, guest, signing_required;
 u8 challenge[8], key[16];
 /* Bounded saved handshake for NTLM MIC verification. */
 u8 negotiate[512], challenge_msg[512];
 unsigned negotiate_len, challenge_len;
};
struct config {
 char *root, *share, *user, *password_file, *hash_file, *bind_address, *random_file;
 char *metadata_file;
 u8 nthash[16];
 unsigned port;
 int guest, max_connections, verbose, writable, max_credits;
};
extern struct config cfg;
void session_revoke(int);
void transport_init(int, FILE *);
int transport_read(u8 *, unsigned);
int transport_prefetch(u8 *, unsigned);
extern struct authstate auth;
extern u8 request[SMBD_BUFSIZE], response[SMBD_BUFSIZE];
extern unsigned request_len, response_len;
/* The complete, already signature-checked command remains in this spool.
 * WRITE streams its payload without allocating a 64 KB PDP-11 buffer. */
extern FILE *request_file;
extern long request_start, request_size;
/* READ can add a file slice after response[]. Transport snapshots it to a
 * private spool before signing and sending, avoiding a mutable-file race. */
extern int read_fd;
extern long read_offset, read_length;
extern u8 related_file[16];

unsigned get16(const u8 *);
u32 get32(const u8 *);
void put16(u8 *, unsigned);
void put32(u8 *, u32);
void put64(u8 *, u32, u32);
int bounds(unsigned, unsigned, unsigned);
void filetime(u8 *, long);
int utf16_decode(char *, unsigned, const u8 *, unsigned);
unsigned utf16_encode(u8 *, const char *, unsigned);

/* Crypto algorithms: 4=MD4, 5=MD5, 256=SHA256. */
void hash_init(struct hashctx *, int);
void hash_update(struct hashctx *, int, const u8 *, unsigned);
void hash_final(struct hashctx *, int, u8 *);
void hmac_init(struct hmacctx *, int, const u8 *, unsigned);
void hmac_update(struct hmacctx *, const u8 *, unsigned);
void hmac_final(struct hmacctx *, u8 *);
int constant_equal(const u8 *, const u8 *, unsigned);
int auth_load(void);
int auth_random_init(void);
void auth_random_close(void);
int auth_random_challenge(void);
u32 auth_session(const u8 *, unsigned, u8 *, unsigned *);
unsigned auth_negotiate(u8 *);

int sessions_init(char *);
int sessions_open(void);
void sessions_child(int, int);
int sessions_lock(void);
void sessions_unlock(void);
int sessions_reaped(int);
void sessions_destroy(void);
int session_establish(u32, u32, u32, u32);
void session_end(void);

void fs_init(void);
/* Each worker holds a separately opened description of the shared registry,
 * so flock serializes operations across processes, including the parent. */
extern int fs_state_fd;
extern int fs_meta_fd;
int metadata_open(struct stat *);
int metadata_reopen(void);
int metadata_init(int, struct stat *, int);
int metadata_get(int, struct stat *, u8 *);
int metadata_set(int, struct stat *, const u8 *);
int metadata_remove(struct stat *);
void fs_reap(int);
void fs_close_all(void);
void fs_close_tree(u32);
u32 fs_dispatch(unsigned);
u32 fs_read_complete(void);
void rpc_init(void);
void rpc_close_all(void);
void rpc_close_tree(u32);
u32 rpc_dispatch(unsigned);

#endif
