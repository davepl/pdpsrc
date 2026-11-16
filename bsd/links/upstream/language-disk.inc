/* Native English UI strings are loaded only when requested. */
#include "language-data.h"
#ifndef X_OK
#define X_OK 1
#endif
/* This include is not scanned by the per-unit header-pruning pass. */
extern unsigned char *path_to_exe;
unsigned char dummyarray[LANGUAGE_DATA_COUNT - 1];
int current_language;
int current_lang_charset;
struct language_item {
 struct language_item *next;
 int id;
 unsigned char text[1];
};
static struct language_item *language_items;
static int language_fd = -1;
static unsigned language_size;

static void language_error()
{
 error("Missing or invalid language.dat; place it beside Links or set LINKS_DATA_DIR");
 exit(RET_FATAL);
}

static void language_read(unsigned char *p, int n)
{
 int k;
 while (n) {
  k = read(language_fd, p, n);
  if (k < 0 && errno == EINTR) continue;
  if (k <= 0) language_error();
  p += k; n -= k;
 }
}

static unsigned language_word(unsigned char *p)
{
 return ((unsigned)p[0] << 8) | p[1];
}

static int language_path(unsigned char *buf, unsigned char *dir, int n)
{
 if (n < 0 || n > 240) return -1;
 memcpy(buf, dir, n);
 if (n && buf[n - 1] != '/') buf[n++] = '/';
 strcpy(buf + n, "language.dat");
 return open(buf, O_RDONLY);
}

static void language_open()
{
 unsigned char buf[256], hdr[12];
 unsigned char *env, *slash, *path, *end;
 int n, len;
 long size;
 if (language_fd != -1) return;
 env = getenv("LINKS_DATA_DIR");
 if (env) language_fd = language_path(buf, env, strlen(env));
 else if (path_to_exe && (slash = strrchr(path_to_exe, '/')))
  language_fd = language_path(buf, path_to_exe, slash - path_to_exe + 1);
 else {
  path = getenv("PATH");
  if (!path) path = "/bin:/usr/bin";
  len = path_to_exe ? strlen(path_to_exe) : 0;
  while (len) {
   end = strchr(path, ':');
   n = end ? end - path : strlen(path);
   if (n + len + 2 < sizeof(buf) && n <= 240) {
    memcpy(buf, path, n);
    if (n) buf[n] = '/';
    memcpy(buf + n + !!n, path_to_exe, len + 1);
    if (!access(buf, X_OK)) { language_fd = language_path(buf, path, n); break; }
   }
   if (!end) break;
   path = end + 1;
  }
 }
 if (language_fd < 0) language_error();
 size = lseek(language_fd, 0L, 2);
 if (size < 12L + 2L * (LANGUAGE_DATA_COUNT + 1) || size > 65535L ||
     lseek(language_fd, 0L, 0) != 0) language_error();
 language_read(hdr, 12);
 if (memcmp(hdr, "LNKENG1\n", 8) || language_word(hdr + 8) != LANGUAGE_DATA_COUNT ||
     (long)language_word(hdr + 10) != size) language_error();
 language_size = (unsigned)size;
}

static unsigned char *language_text(int id)
{
 struct language_item *item;
 unsigned char offsets[4];
 unsigned start, end, len;
 for (item = language_items; item; item = item->next)
  if (item->id == id) return item->text;
 if (id < 0 || id >= LANGUAGE_DATA_COUNT) language_error();
 language_open();
 if (lseek(language_fd, 12L + 2L * id, 0) != 12L + 2L * id) language_error();
 language_read(offsets, 4);
 start = language_word(offsets); end = language_word(offsets + 2);
 if (start < 12 + 2 * (LANGUAGE_DATA_COUNT + 1) || end <= start || end > language_size) language_error();
 len = end - start;
 if (len > 1024) language_error();
 item = mem_alloc(sizeof(struct language_item) + len);
 if (lseek(language_fd, (long)start, 0) != (long)start) language_error();
 language_read(item->text, len);
 if (item->text[len - 1] || memchr(item->text, 0, len - 1)) language_error();
 item->id = id; item->next = language_items; language_items = item;
 return item->text;
}

void init_trans()
{
 current_language = current_lang_charset = 0;
 language_open();
}

void shutdown_trans()
{
 struct language_item *p;
 while (language_items) { p = language_items; language_items = p->next; mem_free(p); }
 if (language_fd != -1) close(language_fd);
 language_fd = -1;
}

unsigned char *get_text_translation(unsigned char *text, struct terminal *term)
{
 if (text < dummyarray || text > dummyarray + (LANGUAGE_DATA_COUNT - 1)) return text;
 return language_text(text - dummyarray);
}

unsigned char *get_english_translation(unsigned char *text)
{
 return get_text_translation(text, NULL);
}

int n_languages() { return 1; }
unsigned char *language_name(int l) { return language_text(2); }
void set_language(int l)
{
 if (l) language_error();
 current_language = current_lang_charset = 0;
}
