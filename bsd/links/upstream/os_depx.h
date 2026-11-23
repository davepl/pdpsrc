#ifndef MAXINT
#ifdef INT_MAX
#define MAXINT INT_MAX
#else
#ifdef PDP11_SMALL
#define MAXINT 32767
#else
#define MAXINT 0x7fffffff
#endif
#endif
#endif

#ifndef SA_RESTART
#define SA_RESTART	0
#endif

/*#ifdef sparc
#define htons(x) (x)
#endif*/

#ifndef HAVE_CFMAKERAW
void cfmakeraw(struct termios *t);
#endif

#ifdef __EMX__
#define strcasecmp stricmp
#define read _read
#define write _write
#endif

#ifdef BEOS
#define socket be_socket
#define connect be_connect
#define getpeername be_getpeername
#define getsockname be_getsockname
#define listen be_listen
#define accept be_accept
#define bind be_bind
#define pipe be_pipe
#define read be_read
#define write be_write
#define close be_close
#define select be_select
#define getsockopt be_getsockopt
#ifndef PF_INET
#define PF_INET AF_INET
#endif
#ifndef SO_ERROR
#define SO_ERROR	10001
#endif
#ifdef errno
#undef errno
#endif
#define errno 1
#endif

#if defined(O_SIZE) && defined(__EMX__)
#define HAVE_OPEN_PREALLOC
#endif


