#define PDP11_UNIT_helpers
#include "links.h"

#ifdef PDP11_NATIVE
void set_point(struct point *p, long x, long y)
{
#if defined(PDP11_NATIVE) || defined(PDP11_COMPACT_POINTS)
 if (x < 0 || x > 255 || y < 0 || y > 255) {
  error("Page exceeds native link-coordinate limit (256 columns/rows)");
  exit(RET_FATAL);
 }
#endif
 p->x = x; p->y = y;
}

#endif

#ifdef PDP11_NATIVE
#include <sys/param.h>
extern char *getwd(char *);
unsigned char *get_cwd()
{
    unsigned char *buf = mem_alloc(MAXPATHLEN + 1);
    if (getwd((char *)buf)) return buf;
    mem_free(buf);
    return NULL;
}
#endif
