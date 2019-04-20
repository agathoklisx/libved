#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>

#include "libved.h"
#include "__libved.h"

static rlcom_t **VED_COMMANDS = NULL;

private int str_eq (const char *sa, const char *sb) {
  for (; *sa == *sb; sa++, sb++)
    if (0 == *sa)
      return 1;

  return 0;
}

private int str_cmp_n (const char *sa, const char *sb, size_t n) {
  for (;n--; sa++, sb++) {
    if (*sa != *sb)
      return (*(unsigned char *)sa - *(unsigned char *)sb);

    if (*sa == 0) return 0;
  }

  return 0;
}

/* in this namespace size has been already computed */
private char *str_dup (const char *src, size_t len) {
  /* avoid recomputation if possible */
  // size_t len = bytelen (src);
  char *dest = Alloc (len + 1);
  if (len)  memcpy (dest, src, len + 1);
  else dest[0] = '\0';
  return dest;
}

/* This is itoa version 0.4, written by Lukás Chmela and released under GPLv3.
 * Original Source:
 * http://www.strudel.org.uk/itoa/
 */

public char *itoa (int value, char *result, int base) {
  if (base < 2 || base > 36) {
    *result = '\0';
    return result;
  }

  char *ptr = result, *ptr1 = result, tmp_char;
  int tmp_value;

  do {
    tmp_value = value;
    value /= base;
    *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"
        [35 + (tmp_value - (value * base))];
  } while (value);

  /* Apply negative sign */
  if (0 > tmp_value)
    *ptr++ = '-';

  *ptr-- = '\0';

  while (ptr1 < ptr) {
    tmp_char = *ptr;
    *ptr--= *ptr1;
    *ptr1++ = tmp_char;
  }

  return result;
}

private void string_free (string_t *this) {
  if (this is NULL) return;
  if (this->mem_size) free (this->bytes);
  free (this);
}

private size_t string_align (size_t size) {
  size_t sz = 8 - (size % 8);
  sz = sizeof (char) * (size + (sz < 8 ? sz : 0));
  return sz;
}

private string_t *string_new (void) {
  string_t *s = AllocType (string);
  s->bytes = Alloc (1);
  s->mem_size = 1;
  s->bytes[0] = '\0';
  return s;
}

private string_t *string_reallocate (string_t *this, size_t len) {
  size_t sz = string_align (this->mem_size + len + 1);
  char *tmp = Realloc (this->bytes, sz);
  this->bytes = tmp;
  this->mem_size = sz;
  return this;
}

private string_t *string_new_with (const char *bytes) {
  string_t *new = AllocType (string);
  size_t len = bytelen (bytes);
  size_t sz = string_align (len + 1);
  char *buf = Alloc (sz);
  memcpy (buf, bytes, len);
  buf[len] = '\0';
  new->bytes = buf;
  new->num_bytes = len;
  new->mem_size = sz;
  return new;
}

private string_t *string_new_with_fmt (const char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  return string_new_with (bytes);
}

private int string_insert_at (string_t *this, const char *bytes, int idx) {
  if (0 > idx) idx = this->num_bytes + idx + 1;
  if (idx < 0 or idx > (int) this->num_bytes) return INDEX_ERROR;

  size_t len = bytelen (bytes);
  ifnot (len) return OK;

  int bts = this->mem_size - (this->num_bytes + len + 1);

  if (1 > bts) string_reallocate (this, len + 1);

  if (idx is (int) this->num_bytes) {
    memcpy (this->bytes + this->num_bytes, bytes, len);
  } else {
    memmove (this->bytes + idx + len, this->bytes + idx,
        this->num_bytes - idx);
    memcpy (this->bytes + idx, bytes, len);
  }

  this->num_bytes += len;
  this->bytes[this->num_bytes] = '\0';
  return OK;
}

private void string_append_byte (string_t *this, char c) {
  int bts = this->mem_size - (this->num_bytes + 2);
  if (1 > bts) string_reallocate (this, 8);
  this->bytes[this->num_bytes++] = c;
  this->bytes[this->num_bytes] = '\0';
}

private void string_insert_byte_at (string_t *this, char c, int idx) {
  char buf[2]; buf[0] = c; buf[1] = '\0';
  string_insert_at (this, buf, idx);
}

private void string_prepend_byte (string_t *this, char c) {
  int bts = this->mem_size - (this->num_bytes + 2);
  if (1 > bts) string_reallocate (this, 8);
  memmove (this->bytes + 1, this->bytes, this->num_bytes++);
  this->bytes[0] = c;
  this->bytes[this->num_bytes] = '\0';
}

private int string_append (string_t *this, const char *bytes) {
  return string_insert_at (this, bytes, -1);
}

private int string_prepend (string_t *this, const char *bytes) {
  return string_insert_at (this, bytes, 0);
}

private int string_append_fmt (string_t *this, const char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];

  va_list ap;
  va_start(ap, fmt);
  vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);

  return string_insert_at (this, bytes, -1);
}

private int string_prepend_fmt (string_t *this, const char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);

  return string_insert_at (this, bytes, 0);
}

private int string_delete_numbytes_at (string_t *this, int num, int idx) {
  if (num < 0) return NOTOK;
  ifnot (num) return OK;

  if (idx < 0 or idx >= (int) this->num_bytes or
      idx + num > (int) this->num_bytes)
    return INDEX_ERROR;

  if (idx + num isnot (int) this->num_bytes)
    memmove (this->bytes + idx, this->bytes + idx + num,
        this->num_bytes - (idx + num - 1));

  this->num_bytes -= num;
  this->bytes[this->num_bytes] = '\0';
  return OK;
}

private int string_replace_numbytes_at_with (
string_t *this, int num, int idx, const char *bytes) {
  if (string_delete_numbytes_at (this, num, idx) isnot OK)
    return NOTOK;

  return string_insert_at (this, bytes, idx);
}

private void string_clear (string_t *this) {
  ifnot (this->num_bytes) return;
  this->bytes[0] = '\0';
  this->num_bytes = 0;
}

private void string_set_nullbyte_at (string_t *this, int idx) {
  if (0 > idx) idx += this->num_bytes;
  if (idx < 0) return;
  if (idx > (int) this->num_bytes) idx = this->num_bytes;
  this->bytes[idx] = '\0';
  this->num_bytes = idx;
}

private int string_replace_with (string_t *this, char *bytes) {
  string_clear (this);
  return string_append (this, bytes);
}

private int string_replace_with_fmt (string_t *this, const char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  return string_replace_with (this, bytes);
}

private void vstr_free (vstr_t *this) {
  if (this is NULL) return;

  vstring_t *vs = this->head;
  while (vs) {
    vstring_t *tmp = vs->next;
    string_free (vs->data);
    free (vs);
    vs = tmp;
  }

  free (this);
}

private string_t *vstr_join (vstr_t *this, char *sep) {
  string_t *bytes = string_new ();
  vstring_t *it = this->head;
  while (it) {
    string_append_fmt (bytes, "%s%s", it->data->bytes, sep);
    it = it->next;
  }

  if (this->num_items)
    string_set_nullbyte_at (bytes, bytes->num_bytes - (NULL is sep ? 0 : bytelen (sep)));

  return bytes;
}

private void vstr_append_current_with (vstr_t *this, char *bytes) {
  vstring_t *vstr = AllocType (vstring);
  vstr->data = string_new_with (bytes);
  current_list_append (this, vstr);
}

private vstr_t *vstr_dup (vstr_t *this) {
  vstr_t *vs = AllocType (vstr);
  vstring_t *it = this->head;
  while (it) {
    vstr_append_current_with (vs, it->data->bytes);
    it = it->next;
  }

  current_list_set (vs, this->cur_idx);
  return vs;
}

private vstr_t *vstr_add_sort_and_uniq (vstr_t *this, char *bytes) {
  vstring_t *vs = AllocType (vstring);
  vs->data = string_new_with (bytes);

  int res = 1;

  if (this->head is NULL) {
    this->head = this->tail = vs;
    this->head->prev = this->head->next = NULL;
    goto theend;
  }

  res = strcmp (bytes, this->head->data->bytes);

  ifnot (res) {
    goto theend;
  } else if (0 > res) {
    this->head->prev = vs;
    vs->next = this->head;
    this->head = vs;
    this->head->prev = NULL;
    goto theend;
  }

  if (this->num_items is 1) {
    res = 1;
    vs->prev = this->head;
    this->tail = vs;
    this->tail->next = NULL;
    this->head->next = vs;
    goto theend;
  }

  res = strcmp (bytes, this->tail->data->bytes);

  if (0 < res) {
    this->tail->next = vs;
    vs->prev = this->tail;
    this->tail = vs;
    this->tail->next = NULL;
    goto theend;
  } else ifnot (res) {
    goto theend;
  }

  vstring_t *it = this->head->next;

  while (it) {
    res = strcmp (bytes, it->data->bytes);
    ifnot (res) goto theend;
    if (0 > res) {
      it->prev->next = vs;
      it->prev->next->next = it;
      it->prev->next->prev = it->prev;
      it->prev = it->prev->next;
      it = it->prev;
      goto theend;
    }
    it = it->next;
  }

theend:
  if (res)
    this->num_items++;
  else {
    string_free (vs->data);
    free (vs);
  }

  return this;
}

private int char_byte_len(uchar c) {
  if (c < 0x80) return 1;
  if ((c & 0xe0) is 0xc0) return 2;
  return 3 + ((c & 0xf0) isnot 0xe0);
}

private char *char_from_code (utf8 c, char *buf) {
  int len = 1;
  if (c < 0x80) {
    buf[0] = (char) c;
  } else if (c < 0x800) {
    buf[0] = (c >> 6) | 0xC0;
    buf[1] = (c & 0x3F) | 0x80;
    len++;
  } else if (c < 0x10000) {
    buf[0] = (c >> 12) | 0xE0;
    buf[1] = ((c >> 6) & 0x3F) | 0x80;
    buf[2] = (c & 0x3F) | 0x80;
    len += 2;
  } else if (c < 0x110000) {
    buf[0] = (c >> 18) | 0xF0;
    buf[1] = ((c >> 12) & 0x3F) | 0x80;
    buf[2] = ((c >> 6) & 0x3F) | 0x80;
    buf[3] = (c & 0x3F) | 0x80;
    len += 3;
  } else
    return NULL;

  buf[len] = '\0';
  return buf;
}

private char *char_nth (char *src, int nth, int len) {
  int n = 0;
  int clen = 0;
  char *line = src;

  for (int i = 0; i < len and n < nth; i++) {
    line += clen;
    clen = char_byte_len (*line);
    n++;
  }

  if (n isnot nth) return src;

  return line;
}

private int char_num (char *bytes, int len) {
  int n = 0;
  int clen = 0;
  char *sp = bytes;

  for (int i = 0; i < len and *sp; i++) {
    sp += clen;
    clen = char_byte_len (*sp);
    n++;
  }

  return n;
}

private utf8 char_get_nth_code (char *src, int nth, int len) {
  char *line = char_nth (src, nth, len);
  if (line is src and nth isnot 1)
    return 0;

  return utf8_code (line);
}

private int char_is_nth_at (char *src, int idx, int len) {
  if (idx >= len) return -1;

  int n = 0;
  int clen = 0;
  char *line = src;

  for (int i = 0; i < len and i <= idx; i++) {

    line += clen;
    if (*line is 0) return -1;
    clen = char_byte_len (*line);
    i += clen - 1;
    n++;
  }

  return n;
}

private int cstring_get_substr_bytes (char *dest, char *buf, int fidx, int lidx) {
  int len = lidx - fidx + 1;
  for (int i = 0; i < len; i++) dest[i] = buf[fidx + i];
  return len;
}

private char *string_reverse_from_to (char *dest, char *src, int fidx, int lidx) {
  int len = lidx - fidx + 1;

  for (int i = 0; i < len; i++) dest[i] = ' ';

  int curidx = 0;
  int tlen = 0;

  uchar c;
  for (int i = fidx; i < len + fidx; i++) {
    c = src[i];
    tlen++;

    if (c < 0x80) {
      dest[len - 1 - curidx++] = c;
      continue;
    }

    int clen = char_byte_len (c);
    tlen += clen - 1;

    for (int ii = 0; ii < clen; ii++) {
      uchar cc = src[i + ii];
      dest[(len - 1 - curidx) - (clen - ii) + 1] = cc;
    }

    curidx += clen;
    i += clen - 1;
  }

  dest[tlen] = '\0';
  return dest;
}

private int re_exec (regexp_t *re, char *bytes, size_t buf_len) {
  (void) buf_len;
  char *sp = strstr (bytes, re->pat->bytes);

  if (NULL is sp) {
    re->retval = RE_NO_MATCH;
    goto theend;
  }

  re->match_idx = sp - bytes;
  re->match_len = re->pat->num_bytes;
  re->match_ptr = bytes + re->match_idx;
  re->retval = re->match_idx + re->match_len;
  re->match = re->pat;

theend:
  return re->retval;
}

private void re_reset_captures (regexp_t *re) {
  re->match_len = re->match_idx = 0;
  re->match_ptr = NULL;

  ifnot (re->flags & RE_PATTERN_IS_STRING_LITERAL) {
    if (re->match isnot NULL) {
      string_free (re->match);
      re->match = NULL;
    }
  }

  for (int i = 0; i < re->num_caps; i++) {
    if (NULL is re->cap[i]) continue;
    free (re->cap[i]);
    re->cap[i] = NULL;
  }
}

private void re_free_captures (regexp_t *re) {
  if (re->cap is NULL) return;
  re_reset_captures (re);
  free (re->cap);
  re->cap = NULL;
}

private void re_allocate_captures (regexp_t *re, int num) {
  re->num_caps = (0 > num ? 0 : num);
  re->cap = Alloc (sizeof (capture_t) * re->num_caps);
}

private void re_free_pat (regexp_t *re) {
  if (NULL is re->pat) return;
  string_free (re->pat);
  re->pat = NULL;
}

private void re_free (regexp_t *re) {
  re_free_pat (re);
  re_free_captures (re);
  free (re);
}

private int re_compile (regexp_t *re) {
  re->flags |= RE_PATTERN_IS_STRING_LITERAL;
  return OK;
}

private regexp_t *re_new (char *pat, int flags, int num_caps, int (*compile) (regexp_t *)) {
  regexp_t *re = AllocType (regexp);
  re->flags |= flags;
  re->pat = string_new_with (pat);
  compile (re);
  re_allocate_captures (re, num_caps);
  re->match = NULL;
  return re;
}

private string_t *re_parse_substitute (regexp_t *re, char *sub, char *replace_buf) {
  (void) re;
  string_t *substr = string_new ();
  char *sub_p = sub;
  while (*sub_p) {
    switch (*sub_p) {
      case '\\':
        if (*(sub_p + 1) is 0) goto theerror;

        switch (*++sub_p) {
          case '&':
            string_append_byte (substr, '&');
            sub_p++;
            continue;

          case '\\':
            string_append_byte (substr, '\\');
            sub_p++;
            continue;

          default:
            goto theerror;
        }

      case '&':
        string_append (substr, replace_buf);
        break;

      default:
        string_append_byte (substr, *sub_p);
     }
    sub_p++;
  }

  return substr;

theerror:
  string_free (substr);
  return NULL;
}

public re_T __init_re__ (void) {
  return ClassInit (re,
    .self = SelfInit (re,
      .exec = re_exec,
      .new = re_new,
      .free = re_free,
      .free_captures = re_free_captures,
      .allocate_captures = re_allocate_captures,
      .reset_captures = re_reset_captures,
      .free_pat = re_free_pat,
      .parse_substitute = re_parse_substitute,
      .compile = re_compile,
    )
  );
}

private int file_is_reg (const char *fname) {
  struct stat st;
  if (NOTOK is stat (fname, &st)) return 0;
  return S_ISREG (st.st_mode);
}

private int file_exists (const char *fname) {
  return (0 is access (fname, F_OK));
}

private int is_directory (char *dname) {
  struct stat st;
  if (NOTOK is stat (dname, &st)) return 0;
  return S_ISDIR (st.st_mode);
}
#define isnot_directory(dname) (0 is is_directory (dname))

private char *dir_get_current (void) {
  size_t size = PATH_MAX;
  char *buf = Alloc (size);
  char *dir = NULL;

  while ((dir = getcwd (buf, size)) is NULL) {
    if (errno isnot ERANGE) break;
    size += (size / 2);
    buf = Realloc (buf, size);
  }

  return dir;
}

private void dirlist_free (dirlist_t *dlist) {
  vstr_free (dlist->list);
  free (dlist);
}

private dirlist_t *dirlist (char *dir) {
  if (NULL is dir) return NULL;
  if (isnot_directory (dir)) return NULL;

  DIR *dh = NULL;
  if (NULL is (dh = opendir (dir))) return NULL;
  struct dirent *dp;

  size_t len;

  dirlist_t *dlist = AllocType (dirlist);
  dlist->free = dirlist_free;
  dlist->list = AllocType (vstr);
  strncpy (dlist->dir, dir, PATH_MAX - 1);

  while (1) {
    errno = 0;

    if (NULL is (dp = readdir (dh)))
      break;

    len = bytelen (dp->d_name);

    if (len < 3 && dp->d_name[0] == '.')
      if (len == 1 || dp->d_name[1] == '.')
        continue;

    vstring_t *vstr = AllocType (vstring);
    vstr->data = string_new_with (dp->d_name);

    if (dp->d_type is DT_DIR) string_append_byte (vstr->data, PATH_SEP);

    current_list_append (dlist->list, vstr);
  }

  closedir (dh);
  return dlist;
}

private char *path_basename (char *name) {
  ifnot (*name) return name;
  char *p = strchr (name, 0);
  if (p is NULL) p = name + bytelen (name) + 1;
  while (p > name and IsNotDirSep (*(p - 1))) --p;
  return p;
}

private char *path_extname (char *name) {
  ifnot (*name) return name;
  char *p = strchr (name, 0);
  if (p is NULL) p = name + bytelen (name) + 1;
  while (p > name and (*(p - 1) isnot '.')) --p;
  if (p is name) return "";
  p--;
  return p;
}

private char *ved_dirname (char *name) {
  size_t len = bytelen (name);
  char *dname = NULL;
  if (name is NULL or 0 is len) {
    dname = Alloc (2); dname[0] = '.'; dname[1] = '\0';
    return dname;
  }

  char *sep = name + len;

  /* trailing slashes */
  while (sep isnot name) {
    ifnot (IS_PATH_SEP (*sep)) break;
    sep--;
  }

  /* first found */
  while (sep isnot name) {
    if (IS_PATH_SEP (*sep)) break;
    sep--;
  }

  /* trim again */
  while (sep isnot name) {
    ifnot (IS_PATH_SEP (*sep)) break;
    sep--;
  }

  if (sep is name) {
    dname = Alloc (2);
    dname[0] = (IS_PATH_SEP (*name)) ? PATH_SEP : '.'; dname[1] = '\0';
    return dname;
  }

  len = sep - name + 1;
  dname = Alloc (len + 1); memcpy (dname, name, len); dname[len] = '\0';
  return dname;
}

#define CONTINUE_ON_EXPECTED_ERRNO(fd__)  \
  if (errno == EINTR) continue;           \
  if (errno == EAGAIN) {                  \
    struct timeval tv;                    \
    fd_set read_fd;                       \
    FD_ZERO(&read_fd);                    \
    FD_SET(fd, &read_fd);                 \
    tv.tv_sec = 0;                        \
    tv.tv_usec = 100000;                  \
    select (fd__ + 1, &read_fd, NULL, NULL, &tv); \
    continue;                             \
   } do {} while (0)

private int fd_read (int fd, char *buf, size_t len) {
  if (1 > len) return NOTOK;

  char *s = buf;
  int bts;
  int tbts = 0;

  while (1) {
    if (NOTOK is (bts = read (fd, s, len))) {
      CONTINUE_ON_EXPECTED_ERRNO (fd);
      return NOTOK;
    }

    tbts += bts;
    if (tbts is (int) len or bts is 0) break;

    s += bts;
  }

  buf[tbts] = '\0';
  return bts;
}

private int fd_write (int fd, char *buf, int len) {
  int retval = len;
  int bts;

  while (len > 0) {
    if (NOTOK is (bts = write (fd, buf, len))) {
      CONTINUE_ON_EXPECTED_ERRNO (fd);
      return NOTOK;
    }

    len -= bts;
    buf += bts;
  }

  return retval;
}

#define TERM_MAKE_PTR_POS_STR(row_, col_) \
({char b__[16];snprintf (b__, 16, TERM_GOTO_PTR_POS_FMT, (row_), (col_)); b__;})

#define TERM_MAKE_COLOR(clr) \
({char b__[8];snprintf (b__, 8, TERM_SET_COLOR_FMT, (clr));b__;})

#define SEND_ESC_SEQ(fd, seq) fd_write ((fd), seq, seq ## _LEN)
#define TERM_SEND_ESC_SEQ(seq) fd_write ($my(out_fd), seq, seq ## _LEN)
#define TERM_GET_ESC_SEQ(seq) \
({char b__[seq ## _LEN + 1];snprintf (b__, seq ## _LEN + 1, "%s", seq); b__;})

private int term_get_ptr_pos (term_t *this, int *row, int *col) {
  if (NOTOK is TERM_SEND_ESC_SEQ (TERM_GET_PTR_POS))
    return NOTOK;

  char buf[32];
  uint i = 0;
  int bts;
  while (i < sizeof (buf) - 1) {
    if (NOTOK is (bts = fd_read ($my(in_fd), buf + i, 1)) or bts is 0)
      return NOTOK;

    if (buf[i] is 'R') break;
    i++;
  }

  buf[i] = '\0';

  if (buf[0] isnot ESCAPE_KEY or buf[1] isnot '[' or
      2 isnot sscanf (buf + 2, "%d;%d", row, col))
    return NOTOK;

  return OK;
}

private void term_set_ptr_pos (term_t *this, int row, int col) {
  char ptr[32];
  snprintf (ptr, 32, TERM_GOTO_PTR_POS_FMT, row, col);
  fd_write ($my(out_fd), ptr, bytelen (ptr));
}

private void term_free (term_t *this) {
  if (NULL is this) return;

  ifnot (NULL is $myprop) {
    free ($myprop);
    $myprop = NULL;
  }
  free (this); this = NULL;
}

/* this is an extended version of the same function of
 * the kilo editor at https://github.com/antirez/kilo.git
 */
private utf8 term_get_input (term_t *this) {
  char c;
  int n;
  char buf[5];

  while (0 is (n = fd_read ($my(in_fd), buf, 1)));

  if (n is NOTOK) return NOTOK;

  c = buf[0];

  switch (c) {
    case ESCAPE_KEY:
      if (fd_read ($my(in_fd), buf, 1) is 0)
         return ESCAPE_KEY;

      /* recent (revailed through CTRL-[other than CTRL sequence]) and unused */
      if ('z' >= buf[0] and buf[0] >= 'a') {
        /*
         * if (fd_read ($my(in_fd), buf + 1, 1) is 0)
         *   return ALT(buf[0]);
         * else
         */
        return 0;
      }

      if (buf[0] isnot '[' && buf[0] isnot 'O')
        return 65535 + buf[0];

      ifnot (fd_read ($my(in_fd), buf + 1, 1))
        return ESCAPE_KEY;

      if (buf[0] == '[') {
        if ('0' <= buf[1] && buf[1] <= '9') {
          ifnot (fd_read ($my(in_fd), buf + 2, 1))
             return ESCAPE_KEY;

          if (buf[2] == '~') {
            switch (buf[1]) {
              case '1': return HOME_KEY;
              case '2': return INSERT_KEY;
              case '3': return DELETE_KEY;
              case '4': return END_KEY;
              case '5': return PAGE_UP_KEY;
              case '6': return PAGE_DOWN_KEY;
              case '7': return HOME_KEY;
              case '8': return END_KEY;
              default: return 0;
            }
          } else if (buf[1] == '1') {
            if (fd_read ($my(in_fd), buf, 1) is 0)
              return ESCAPE_KEY;

            switch (buf[2]) {
              case '1': return FN_KEY(1);
              case '2': return FN_KEY(2);
              case '3': return FN_KEY(3);
              case '4': return FN_KEY(4);
              case '5': return FN_KEY(5);
              case '7': return FN_KEY(6);
              case '8': return FN_KEY(7);
              case '9': return FN_KEY(8);
              default: return 0;
            }
          } else if (buf[1] == '2') {
            if (fd_read ($my(in_fd), buf, 1) is 0)
              return ESCAPE_KEY;

            switch (buf[2]) {
              case '0': return FN_KEY(9);
              case '1': return FN_KEY(10);
              case '3': return FN_KEY(11);
              case '4': return FN_KEY(12);
              default: return 0;
            }
          } else { /* CTRL_[key other than CTRL sequence] */
                   /* lower case */
            if (buf[2] == 'h')
              return INSERT_KEY; /* sample/test (logically return 0) */
            else
              return 0;
          }
        } else if (buf[1] == '[') {
          if (fd_read ($my(in_fd), buf, 1) is 0)
            return ESCAPE_KEY;

          switch (buf[0]) {
            case 'A': return FN_KEY(1);
            case 'B': return FN_KEY(2);
            case 'C': return FN_KEY(3);
            case 'D': return FN_KEY(4);
            case 'E': return FN_KEY(5);

            default: return 0;
          }
        } else {
          switch (buf[1]) {
            case 'A': return ARROW_UP_KEY;
            case 'B': return ARROW_DOWN_KEY;
            case 'C': return ARROW_RIGHT_KEY;
            case 'D': return ARROW_LEFT_KEY;
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            case 'P': return DELETE_KEY;

            default: return 0;
          }
        }
      } else if (buf[0] == 'O') {
        switch (buf[1]) {
          case 'A': return ARROW_UP_KEY;
          case 'B': return ARROW_DOWN_KEY;
          case 'C': return ARROW_RIGHT_KEY;
          case 'D': return ARROW_LEFT_KEY;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
          case 'P': return FN_KEY(1);
          case 'Q': return FN_KEY(2);
          case 'R': return FN_KEY(3);
          case 'S': return FN_KEY(4);

          default: return 0;
        }
      }
    break;

  default:
    if (c < 0) {
      int len = char_byte_len ((uchar) c);
      utf8 code = 0;
      code += (uchar) c;

      int idx;
      int invalid = 0;
      char cc;

      for (idx = 0; idx < len - 1; idx++) {
        if (0 >= fd_read ($my(in_fd), &cc, 1))
          return NOTOK;

        if (isnotutf8 ((uchar) cc)) {
          invalid = 1;
        } else {
          code <<= 6;
          code += (uchar) cc;
        }
      }

      if (invalid)
        return NOTOK;

      code -= offsetsFromUTF8[len-1];
      return code;
    }

    if (127 == c)
      return BACKSPACE_KEY;

    return c;
  }

  return NOTOK;
}

private term_t *term_new (void) {
  term_t *this = AllocType (term);
  $myprop = AllocProp (term);
  $my(is_initialized) = 0;
  $my(lines) = 24;
  $my(columns) = 78;
  $my(out_fd) = STDOUT_FILENO;
  $my(in_fd) = STDIN_FILENO;

  return this;
}

private int term_sane (term_t *this) {
  if (isnotatty ($my(in_fd))) return NOTOK;

  struct termios mode;
  while (NOTOK is tcgetattr ($my(in_fd), &mode))
    if (errno isnot EINTR) return NOTOK;

  mode.c_iflag |= (BRKINT|INLCR|ICRNL|IXON|ISTRIP);
  mode.c_iflag &= ~(IGNBRK|INLCR|IGNCR|IXOFF);
  mode.c_oflag |= (OPOST|ONLCR);
  mode.c_lflag |= (ECHO|ECHOE|ECHOK|ECHOCTL|ISIG|ICANON|IEXTEN);
  mode.c_lflag &= ~(ECHONL|NOFLSH|XCASE|TOSTOP|ECHOPRT);
  mode.c_cc[VEOF] = 'D'^64; // splitvt
  mode.c_cc[VMIN] = 1;   /* 0 */
  mode.c_cc[VTIME] = 0;  /* 1 */

  while (NOTOK is tcsetattr ($my(in_fd), TCSAFLUSH, &mode))
    if (errno isnot EINTR) return NOTOK;

  return OK;
}

private int term_raw (term_t *this) {
  if ($my(is_initialized) is 1) return OK;
  if (isnotatty ($my(in_fd))) return NOTOK;

  while (NOTOK is tcgetattr ($my(in_fd), &$my(orig_mode)))
    if (errno isnot EINTR) return NOTOK;

  $my(raw_mode) = $my(orig_mode);
  $my(raw_mode).c_iflag &= ~(INLCR|ICRNL|IXON|ISTRIP);
  $my(raw_mode).c_cflag |= (CS8);
  $my(raw_mode).c_oflag &= ~(OPOST);
  $my(raw_mode).c_lflag &= ~(ECHO|ISIG|ICANON|IEXTEN);
  $my(raw_mode).c_lflag &= NOFLSH;
  $my(raw_mode).c_cc[VEOF] = 1;
  $my(raw_mode).c_cc[VMIN] = 0;   /* 1 */
  $my(raw_mode).c_cc[VTIME] = 1;  /* 0 */

  while (NOTOK is tcsetattr ($my(in_fd), TCSAFLUSH, &$my(raw_mode)))
    ifnot (errno is EINTR) return NOTOK;

  $my(is_initialized) = 1;
  return OK;
}

private void term_restore (term_t *this) {
  ifnot ($my(is_initialized)) return;
  if (isnotatty ($my(in_fd))) return;

  while (NOTOK is tcsetattr ($my(in_fd), TCSAFLUSH, &$my(orig_mode)))
    ifnot (errno is EINTR) return;

  $my(is_initialized) = 0;
}

private void term_get_size (term_t *this, int *rows, int *cols) {
  struct winsize wsiz;

  do {
    if (OK is ioctl ($my(out_fd), TIOCGWINSZ, &wsiz)) {
      $my(lines) = (int) wsiz.ws_row;
      $my(columns) = (int) wsiz.ws_col;
      return;
    }
  } while (errno is EINTR);

  int orig_row, orig_col;
  term_get_ptr_pos (this, &orig_row, &orig_col);

  TERM_SEND_ESC_SEQ (TERM_LAST_RIGHT_CORNER);
  term_get_ptr_pos (this, rows, cols);
  term_set_ptr_pos (this, orig_row, orig_col);
}

private void term_screen_save (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_SCREEN_SAVE);
}

private void term_screen_restore (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_SCREEN_RESTORE);
}

private void term_screen_clear (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_SCREEN_CLEAR);
}

private void term_screen_clear_eol (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_LINE_CLR_EOL);
}

private void term_cursor_hide (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_CURSOR_HIDE);
}

private void term_cursor_restore (term_t *this) {
  TERM_SEND_ESC_SEQ (TERM_CURSOR_RESTORE);
}

private video_t *video_new (int fd, int rows, int cols, int first_row, int first_col) {
  video_t *this = AllocType (video);

  loop (rows) {
    vrow_t *row = AllocType (vrow);
    row->data = string_new_with (" ");
    current_list_append (this, row);
  }

  this->fd = fd;
  this->first_row = first_row;
  this->first_col = first_col;
  this->num_rows = rows;
  this->num_cols = cols;
  this->last_row = first_row + rows - 1;
  this->row_pos = this->first_row;
  this->col_pos = 1;
  this->render = string_new ();
  return this;
}

private void video_free (video_t *this) {
  if (this is NULL) return;

  vrow_t *it = this->head;

  while (it isnot NULL) {
    vrow_t *next = it->next;
    string_free (it->data);
    free (it);
    it = next;
  }

  string_free (this->render);

  free (this);
}

private void video_flush (video_t *this, string_t *render) {
  fd_write (this->fd, render->bytes, render->num_bytes);
}

private void video_render_append_line_with_current (video_t *this) {
  string_append_fmt (this->render, "%s%s%s",
     TERM_LINE_CLR_EOL, this->current->data->bytes, TERM_BOL);
}

private void video_render_set_from_to (video_t *this, int frow, int lrow) {
  int fidx = frow - 1; int lidx = lrow - 1;

  string_append (this->render, TERM_CURSOR_HIDE);
  while (fidx <= lidx) {
    current_list_set (this, fidx++);
    string_append_fmt (this->render, TERM_GOTO_PTR_POS_FMT "%s%s%s",
      this->cur_idx + 1, 0, TERM_LINE_CLR_EOL, this->current->data->bytes, TERM_BOL);
  }

  string_append (this->render, TERM_CURSOR_SHOW);
}

private void video_draw_at (video_t *this, int at) {
  int idx = at - 1;

  if (current_list_set(this, idx) is INDEX_ERROR) return;

  vrow_t *row = this->current;

  string_t *render = string_new_with_fmt (
      "%s" TERM_GOTO_PTR_POS_FMT "%s%s%s" TERM_GOTO_PTR_POS_FMT,
      TERM_CURSOR_HIDE, at, 0, TERM_LINE_CLR_EOL,
      row->data->bytes, TERM_CURSOR_SHOW, this->row_pos, this->col_pos);

  video_flush (this, render);
  string_free (render);
}

private void video_show_cursor (video_t *this) {
  string_replace_with_fmt (this->render, "%s\r",  TERM_CURSOR_SHOW);
  video_flush (this, this->render);
}

private void video_draw_all (video_t *this) {
  vrow_t *row, *next;
  int num_times = this->last_row;

  string_t *render = string_new_with_fmt ("%s%s", TERM_CURSOR_HIDE, TERM_FIRST_LEFT_CORNER);
  string_append_fmt (render, TERM_SCROLL_REGION_FMT, 0, this->num_rows);
  string_append     (render, "\033[0m\033[1m");

  row = this->head;

  loop (num_times - 1) {
    next = row->next;
    string_append_fmt (render, "%s%s%s", TERM_LINE_CLR_EOL, row->data->bytes, TERM_BOL);
    row = next;
  }

  string_append_fmt (render, "%s" TERM_GOTO_PTR_POS_FMT,
     TERM_CURSOR_SHOW, this->row_pos, this->col_pos);

  video_flush (this, render);
  string_free (render);
}

private void video_set_row_with (video_t *this, int idx, char *bytes) {
  if (current_list_set (this, idx) is INDEX_ERROR) return;
  vrow_t *row = this->current;
  string_replace_with (row->data, bytes);
}

private void video_resume_painted_rows (video_t *this) {
  int row = 0, i = 0;
  while (0 isnot (row = this->rows[i++])) video_draw_at (this, row);
  this->rows[0] = 0;
}

private video_t *video_paint_rows_with (video_t *this, int row, int f_col, int l_col, char *bytes) {
  int last_row = this->last_row - 3;
  int first_row = row < 1 or row > last_row ? last_row : row;
  int last_col = l_col < 0 or l_col > this->num_cols ? this->num_cols : l_col;
  int first_col = f_col < 0 or f_col > this->num_cols ? 1 : f_col;
  char *f_p = bytes;
  char *l_p = bytes;

  vstr_t *vsa = AllocType (vstr);

  while (l_p isnot NULL) {
    vstring_t *vs = AllocType (vstring);
    vs->data = string_new ();

    l_p = strstr (l_p, "\n");

    if (NULL is l_p) {
      string_append (vs->data, f_p);
    } else {
      char line[l_p - f_p];
      int i = 0;
      while (f_p < l_p) line[i++] = *f_p++;
      f_p++; l_p++;
      line[i] = '\0';
      string_append (vs->data, line);
    }

    current_list_append (vsa, vs);
  }

  int num_rows = vsa->num_items;
  int i = 0; while (i < num_rows - 1 and first_row > 2) {i++; first_row--;};
  num_rows = i + 1;
  int num_chars = last_col - first_col + 1;

  string_t *render = string_new_with_fmt ("%s" TERM_SET_COLOR_FMT, TERM_CURSOR_HIDE, COLOR_BOX);
  vstring_t *it = vsa->head;
  i = 0;
  while (i < num_rows) {
    this->rows[i] = (i + first_row);
    string_append_fmt (render, TERM_GOTO_PTR_POS_FMT, first_row + i++, first_col);
    int num = 0; int idx = 0;
    while (num++ < num_chars and idx < (int) it->data->num_bytes) {
      int clen = char_byte_len (it->data->bytes[idx]);
      for (int li = 0; li < clen; li++)
        string_append_byte (render, it->data->bytes[idx + li]);
      idx += clen;
    }

    while (num++ < num_chars) string_append_byte (render, ' ');

    it = it->next;
  }

  this->rows[num_rows] = 0;

  string_append_fmt (render, "%s%s", TERM_COLOR_RESET, TERM_CURSOR_SHOW);
  video_flush (this, render);
  string_free (render);
  vstr_free (vsa);
  return this;
}

private void menu_free_list (menu_t *this) {
  if (this->state & MENU_LIST_IS_ALLOCATED) {
    vstr_free (this->list);
    this->state &= ~MENU_LIST_IS_ALLOCATED;
  }
}

private void menu_free (menu_t *this) {
  menu_free_list (this);
  string_free (this->header);
  free (this);
}

private menu_t *menu_new (ed_t *this, int first_row, int last_row, int first_col,
int (*process_list) (menu_t *), char *pat, size_t patlen) {
  menu_t *menu = AllocType (menu);
  menu->fd = $my(video)->fd;
  menu->first_row = first_row;
  menu->orig_first_row = first_row;
  menu->min_first_row = 3;
  menu->last_row = last_row;
  menu->num_rows = menu->last_row - menu->first_row + 1;
  menu->first_col = first_col + 1;
  menu->num_cols = $my(dim)->num_cols;
  menu->cur_video = $my(video);
  menu->process_list = process_list;
  menu->state |= (MENU_INIT|RL_IS_VISIBLE);
  menu->space_selects = 1;
  menu->header = string_new ();
  menu->this = self(get.current_buf);
  ifnot (NULL is pat) {
    memcpy (menu->pat, pat, patlen);
    menu->patlen = patlen;
  }

  menu->retval = menu->process_list (menu);
  return menu;
}

private void menu_clear (menu_t *menu) {
  if (menu->header->num_bytes)
    video_draw_at (menu->cur_video, menu->first_row - 1);

  for (int i = 0;i < menu->num_rows; i++)
    video_draw_at (menu->cur_video, menu->first_row + i);
}

private int rline_menu_at_beg (rline_t **rl) {
  switch ((*rl)->c) {
    case ESCAPE_KEY:
    case '\r':
    case ARROW_LEFT_KEY:
    case ARROW_RIGHT_KEY:
    case ARROW_UP_KEY:
    case ARROW_DOWN_KEY:
    case '\t':
    (*rl)->state |= RL_POST_PROCESS;
    return RL_POST_PROCESS;
  }

  (*rl)->state |= RL_OK;
  return RL_OK;
}

private char *menu_create (ed_t *this, menu_t *menu) {
  rline_t *rl = rline_new (this, $my(term), My(Input).get, $my(prompt_row),
    $my(dim)->num_cols, $my(video));
  rl->at_beg = rline_menu_at_beg;
  rl->at_end = rline_break;
  rl->state |= RL_CURSOR_HIDE;
  rl->prompt_char = 0;

  ifnot (menu->state & RL_IS_VISIBLE) rl->state &= ~RL_IS_VISIBLE;

  BYTES_TO_RLINE (rl, menu->pat, menu->patlen);

init_list:;
  if (menu->state & MENU_REINIT_LIST) {
    menu->state &= ~MENU_REINIT_LIST;
    menu_clear (menu);
    menu->first_row = menu->orig_first_row;
  }

  char *match = NULL;

  ifnot (menu->list->num_items) goto theend;

  vstring_t *it = menu->list->head;

  int maxlen = 0;
  while (it) {
    if ((int) it->data->num_bytes > maxlen) maxlen = it->data->num_bytes;
    it = it->next;
  }

  ifnot (maxlen) goto theend;
  maxlen++;

  int avail_cols = menu->num_cols - menu->first_col;
  int num = 1;

  if (maxlen < avail_cols) {
    num = avail_cols / maxlen;
    if ((num - 1) + (maxlen * num) > avail_cols)
      num--;
  }

  int mod = menu->list->num_items % num;
  int num_cols = (num * maxlen);
  int num_rows = menu->list->num_items / num + (mod isnot 0);
  int rend_rows = menu->num_rows;
  int first_row = menu->first_row;
  int first_col = menu->first_col;

  if (num_rows > rend_rows) {
    while (first_row > menu->min_first_row and num_rows > rend_rows) {
      first_row--;
      rend_rows++;
    }
  } else {
    rend_rows = num_rows;
  }

  menu->num_rows = rend_rows;
  menu->first_row = first_row;

  //char *fmt = str_fmt ("\033[%%dm%%-%ds%%s", maxlen);
  char *fmt =  str_fmt ("\033[%%dm%%-%ds%%s", maxlen);
//debug_append (fmt);
  int cur_idx = 0;
  int frow_idx = 0;
  int vrow_pos = first_row;
  int vcol_pos = 1;

  for (;;) {
    it = menu->list->head;
    for (int i = 0; i < frow_idx; i++) {
      for (int i__ = 0; i__ < num; i__++)
        it = it->next;
    }

    string_t *render = string_new_with (TERM_CURSOR_HIDE);
    if (menu->header->num_bytes) {
      string_append_fmt (render, TERM_GOTO_PTR_POS_FMT TERM_SET_COLOR_FMT,
         first_row - 1, first_col, COLOR_MENU_HEADER);

      if ((int) menu->header->num_bytes > num_cols) {
        My(String).clear_at (menu->header, num_cols - 1);
      } else
      while ((int) menu->header->num_bytes < num_cols)
        My(String).append_byte (menu->header, ' ');

      string_append_fmt (render, "%s%s", menu->header->bytes, TERM_COLOR_RESET);
    }

    int ridx, iidx = 0; int start_row = 0;
    for (ridx = 0; ridx < rend_rows; ridx++) {
      start_row = first_row + ridx;
      string_append_fmt (render, TERM_GOTO_PTR_POS_FMT, start_row, first_col);

      for (iidx = 0; iidx < num and iidx + (ridx  * num) + (frow_idx * num) < menu->list->num_items; iidx++) {
        if (iidx + (ridx * num) + (frow_idx * num) is cur_idx) {
          string_append_fmt (render, fmt, COLOR_MENU_SEL, it->data->bytes, TERM_COLOR_RESET);
        }
        else
          string_append_fmt (render, fmt, COLOR_MENU_BG, it->data->bytes, TERM_COLOR_RESET);

        it = it->next;
      }

      if (mod)
        for (int i = mod + 1; i < num; i++)
          for (int ii = 0; ii < maxlen; ii++)
            string_append_byte (render, ' ');
   }

//    string_append (render, TERM_CURSOR_SHOW);

    fd_write (menu->fd, render->bytes, render->num_bytes);

    string_free (render);

    menu->c = rline_edit (rl)->c;

    switch (menu->c) {
      case ESCAPE_KEY: goto theend;

      case '\r':
      case ' ':
        if (' ' is menu->c and menu->space_selects is 0)
          goto insert_char;

        it = menu->list->head;
        for (int i = 0; i < cur_idx; i++) it = it->next;
        match = it->data->bytes;
        goto theend;

      case ARROW_LEFT_KEY:
        ifnot (0 is cur_idx) {
          if (vcol_pos > 1) {
            cur_idx--;
            vcol_pos--;
            continue;
          }

          cur_idx += (num - vcol_pos);
          vcol_pos += (num - vcol_pos);
        }

        //__fallthrough__;

      case ARROW_UP_KEY:
        if (vrow_pos is first_row) {
          ifnot (frow_idx) {
            cur_idx = menu->list->num_items - 1;
            vcol_pos = 1 + (mod ? mod - 1 : 0);
            frow_idx = num_rows - rend_rows;
            vrow_pos = first_row + rend_rows - 1;
            continue;
          }

          frow_idx--;
        } else
          vrow_pos--;

        cur_idx -= num;
        continue;

      case '\t':
      case ARROW_RIGHT_KEY:
         if (vcol_pos isnot num and cur_idx < menu->list->num_items - 1) {
           cur_idx++;
           vcol_pos++;
           continue;
         }

        cur_idx -= (vcol_pos - 1);
        vcol_pos = 1;

        //__fallthrough__;

      case ARROW_DOWN_KEY:
        if (vrow_pos is menu->last_row or (vrow_pos - first_row + 1 is num_rows)) {
          if (cur_idx + (num - vcol_pos) + 1 >= menu->list->num_items) {
            frow_idx = 0;
            vrow_pos = first_row;
            cur_idx = 0;
            vcol_pos = 1;
            continue;
          }

          cur_idx += num;
          while (cur_idx >= menu->list->num_items) {cur_idx--; vcol_pos--;}
          frow_idx++;
          continue;
        }

        cur_idx += num;
        while (cur_idx >= menu->list->num_items) {cur_idx--; vcol_pos--;}
        vrow_pos++;
        continue;

      case PAGE_UP_KEY: {
          int i = rend_rows;
          while (i--) {
            if (frow_idx > 0) {
              frow_idx--;
              cur_idx -= num;
            } else break;
          }
        }

        continue;

      case PAGE_DOWN_KEY: {
          int i = rend_rows;
          while (i--) {
            if (frow_idx < num_rows - rend_rows) {
              frow_idx++;
              cur_idx += num;
            } else break;
          }
        }

        continue;

      default:
insert_char:
        {
          string_t *p = vstr_join (rl->line, "");
          if (rl->line->tail->data->bytes[0] is ' ')
            string_set_nullbyte_at (p, p->num_bytes - 1);

          ifnot (str_eq (menu->pat, p->bytes)) {
            menu->patlen = p->num_bytes;
            strcpy (menu->pat, p->bytes);
            menu->pat[menu->patlen] = '\0';
          }

          string_free (p);
        }

        menu->process_list (menu);
        if (menu->state & MENU_QUIT) goto theend;
        if (menu->state & MENU_REINIT_LIST) goto init_list;

        continue;
    }
  }

theend:
  menu_clear (menu);
  rline_free (rl);
  return match;
}

private dim_t *dim_set (dim_t *dim, int f_row, int l_row, int f_col, int l_col) {
  dim->first_row = f_row;
  dim->last_row = l_row;
  dim->num_rows = l_row - f_row + 1;
  dim->first_col = f_col;
  dim->last_col = l_col;
  dim->num_cols = l_col - f_col + 1;
  return dim;
}

private dim_t *dim_new (int f_row, int l_row, int f_col, int l_col) {
  dim_t *dim = AllocType (dim);
  return dim_set (dim, f_row, l_row, f_col, l_col);
}

private dim_t **dim_calc (win_t *this,
int num_rows, int num_frames, int min_rows, int has_dividers) {
  int reserved = $my(has_topline) + $my(has_msgline) + $my(has_promptline);
  int dividers = has_dividers ? num_frames - 1 : 0;
  int rows = (num_frames * min_rows) + dividers + reserved;

  if (num_rows < rows) return NULL;

  rows = (num_rows - dividers - reserved) / num_frames;
  int mod = (num_rows - dividers - reserved) % num_frames;

  dim_t **dims = Alloc (sizeof (dims) * num_frames);

  for (int i = 0; i < num_frames; i++) {
    dims[i] = AllocType (dim);
    dims[i]->num_rows = rows + (i is 0 ? mod : 0);
    dims[i]->first_row = i is 0
      ? 1 + $my(has_topline)
      : dims[i-1]->last_row + has_dividers + 1;
    dims[i]->last_row = dims[i]->first_row + dims[i]->num_rows - 1;
    dims[i]->first_col = $my(dim->first_col);
    dims[i]->last_col  = $my(dim->last_col);
    dims[i]->num_cols  = $my(dim->num_cols);
  }

  return dims;
}

/* a highlight theme derived from tte editor, fork of kilo editor,
 * adjusted for the environment.
 * written by itself (the very first lines)
 */

private char *ved_syn_parse_c (buf_t *, char *, int, int, row_t *);
private char *ved_syn_parse_default (buf_t *, char *, int, int, row_t *);
private ftype_t *buf_default_ftype (buf_t *);
private ftype_t *buf_ftype_init_c (buf_t *);

char *default_extensions[] = {".txt", NULL};
char *C_extensions[] = {".c", ".h", ".cpp", ".hpp", ".cc", NULL};
char *C_keywords[] = {
    "is", "isnot", "or", "and", "loop", "ifnot",
    "private|", "public|", "self|", "this|", "$my|", "$myprop|", "My|",
    "$mycur|", "$from|", "theend|", "OK|", "NOTOK|", "mutable|",
    "forever", "__fallthrough__",
    "switch", "if", "while", "for", "break", "continue", "else", "do",
    "default", "goto",
    "case|", "return|", "free|",
    "struct|", "union|", "typedef|", "static|", "enum|", "#include|",
    "volatile|", "register|", "sizeof|", "union|",
    "const|", "auto|",
    "#define|", "#endif|", "#error|", "#ifdef|", "#ifndef|", "#undef|", "#if|",
    "int|",  "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", "bool|", "NULL|",
    NULL
};

#define FTYPE_DEFAULT 0
#define FTYPE_C 1

syn_t HL_DB[] = {
  [FTYPE_DEFAULT] = {
    "txt",
    default_extensions,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0,
    ved_syn_parse_default,
    buf_default_ftype,
    0,
  },
  [FTYPE_C] =
    {
    "c",
    C_extensions,
    C_keywords,
    "+:-%*^><=|&~.()[]{}!",
    "//",
    "/*",
    "*/",
    1,
    1,
    ved_syn_parse_c,
    buf_ftype_init_c,
    0,
    }
};

#define IGNORE(c) ((c) > 0x80 || (c) <= ' ')
#define ADD_COLORED_CHAR(c, clr) snprintf (ccbuf, 16,     \
  TERM_SET_COLOR_FMT "%c" TERM_COLOR_RESET, (clr), (c));  \
  My(String).append (s, ccbuf)
#define ADD_INVERTED_CHAR(c, clr) snprintf (ccbuf, 16,                   \
  TERM_INVERTED "%s%c" TERM_COLOR_RESET, TERM_MAKE_COLOR ((clr)), (c));  \
  My(String).append (s, ccbuf)

#define SYN_HAS_OPEN_COMMENT  (1 << 0)
#define SYN_HAS_SINGLELINE_COMMENT (1 << 1)
#define SYN_HAS_MULTILINE_COMMENT (1 << 2)

private char *ved_syn_parse_default (buf_t *this, char *line, int len, int index, row_t *row) {
  (void) index; (void) row;
  ifnot (len) return line;

  string_t *s = My(String).new ();
  char ccbuf[16];

  uchar c;
  int idx = 0;
  for (idx = 0; idx < len; idx++) {

    c = line[idx];

parse_char:;
    while (IGNORE (c)) {
      if (c is ' ') {
        if (idx + 1 is len) {
          ADD_INVERTED_CHAR (' ', HL_TRAILING_WS);
        }
        else
          My(String).append_byte (s, c);
      }
      else
        if (c is '\t') {
          ADD_INVERTED_CHAR (' ', HL_TAB);
        }
        else
          if ((c < ' ' and (c isnot 0 and c isnot 0x0a)) or c is 0x7f) {
            ADD_INVERTED_CHAR ('?', HL_ERROR);
          }
          else {
            My(String).append_byte (s,  c);
          }

      if (++idx is len) goto theend;
      c = line[idx];
    }

    ifnot (NULL is $my(syn->operators)) {
      ifnot (NULL is strchr ($my(syn)->operators, c)) {
        ADD_COLORED_CHAR (c, HL_OPERATOR);
        if (++idx is len) goto theend;
        c = line[idx];
        ifnot (NULL is strchr ($my(syn)->operators, c)) { /* most likely one or two */
            ADD_COLORED_CHAR (c, HL_OPERATOR);
            goto thecontinue;
        }
        goto parse_char;
      }
    }

    if ($my(syn)->hl_strings) {
      if (c is '"' or c is '\'') {
        ADD_COLORED_CHAR (c, HL_STR_DELIM);
        while (++idx < len) {
          c = line[idx];
          if (c is '"' or c is '\'') { /* handle case '"' "'" '\'' "\"" */
            ADD_COLORED_CHAR (c, HL_STR_DELIM);
            goto thecontinue;
          }

          My(String).append_byte (s,  c);
        }

        goto theend;
      }
    }

    if ($my(syn)->hl_numbers) {
      if (IS_DIGIT (c)) {
        ADD_COLORED_CHAR (c, HL_NUMBER);
        while (++idx < len) {
          c = line[idx];
          if (0 is IS_DIGIT (c) and 0 is IsAlsoANumber (c))
            goto parse_char; // most likely next loop is useless

          ADD_COLORED_CHAR (c, HL_NUMBER);
        }

        goto theend;
      }
    }

    ifnot (NULL is $my(syn)->keywords) {
      for (int j = 0; $my(syn)->keywords[j] isnot NULL; j++) {
        int kw_len = bytelen($my(syn)->keywords[j]);
        int kw_2 = $my(syn)->keywords[j][kw_len - 1] is '|';

        if (kw_2) kw_len--;

        if (0 == str_cmp_n (&line[idx], $my(syn)->keywords[j], kw_len) and
            IsSeparator (line[idx + kw_len])) {

          char kw[kw_len]; memcpy (kw, &line[idx], kw_len); kw[kw_len] = '\0';

          My(String).append_fmt (s, "%s%s%s",
            TERM_MAKE_COLOR (kw_2 ? HL_IDENTIFIER : HL_KEYWORD), kw, TERM_COLOR_RESET);

          idx += kw_len - 1;
          goto thecontinue;
        }
      }
    }

    My(String).append_byte (s,  c);

thecontinue: {}
  }

theend:
  strncpy (line, s->bytes, s->num_bytes);
  line[s->num_bytes] = '\0';
  My(String).free (s);

  return line;
}

private char *ved_syn_parse_c (buf_t *this, char *line, int len, int index, row_t *row) {
 (void) index;
  ifnot (len) return line;

  string_t *s = My(String).new ();
  char ccbuf[16];

  char *m_cmnt_p = strstr (line, $my(syn)->multiline_comment_start);
  int m_cmnt_idx = (NULL is m_cmnt_p ? len : m_cmnt_p - line);

  char *s_cmnt_p = strstr (line, $my(syn)->singleline_comment_start);
  int s_cmnt_idx = (NULL is s_cmnt_p ? len : s_cmnt_p - line);

  int has_mlcmnt = ({
    int found = 0;
    row_t *it = row->prev;
    for (int i = 0; i < 7 and it; i++) {
      if (NULL isnot strstr (it->data->bytes, $my(syn)->multiline_comment_end))
        break;

      if (NULL isnot strstr (it->data->bytes, $my(syn)->multiline_comment_start) or
          0 is str_cmp_n (it->data->bytes, " * ", 3)) {
        found = 1;
        break;
      }

      it = it->prev;
    }

    found;
  });

  uchar c;
  int idx = 0;
  for (idx = 0; idx < len; idx++) {

   c = line[idx];

parse_char:;
open_comment:;
    if ($my(syn)->state & SYN_HAS_OPEN_COMMENT) {
      $my(syn)->state &= ~SYN_HAS_OPEN_COMMENT;
      int diff = len;
      My(String).append_fmt (s, "%s%s", TERM_MAKE_COLOR (HL_COMMENT), TERM_ITALIC);
      if ($my(syn)->state & SYN_HAS_MULTILINE_COMMENT) {
        $my(syn)->state &= ~SYN_HAS_MULTILINE_COMMENT;
        char *sp = strstr (line + idx, $my(syn)->multiline_comment_end);
        if (sp is NULL) {
          while (idx < len)
            My(String).append_byte (s, line[idx++]);

          My(String).append (s, TERM_COLOR_RESET);
          goto theend;
        }

        diff = idx + (sp - (line + idx)) + (int) bytelen ($my(syn)->multiline_comment_end);

      } else
        $my(syn)->state &= ~SYN_HAS_SINGLELINE_COMMENT;

      while (idx < diff) My(String).append_byte (s, line[idx++]);
      My(String).append (s, TERM_COLOR_RESET);

      if (idx is len) goto theend;
      c = line[idx];
    }

    if (has_mlcmnt or idx is m_cmnt_idx) {
      has_mlcmnt = 0;
      $my(syn)->state |= (SYN_HAS_OPEN_COMMENT|SYN_HAS_MULTILINE_COMMENT);
      goto open_comment;
    }

    if (idx is s_cmnt_idx) {
      $my(syn)->state |= (SYN_HAS_OPEN_COMMENT|SYN_HAS_SINGLELINE_COMMENT);
      goto open_comment;
    }

    while (IGNORE (c)) {
      if (c is ' ') {
        if (idx + 1 is len) {
          ADD_INVERTED_CHAR (' ', HL_TRAILING_WS);
        }
        else
          My(String).append_byte (s,  c);
      }
      else
        if (c is '\t') {
          ADD_INVERTED_CHAR (' ', HL_TAB);
        }
        else
          if ((c < ' ' and (c isnot 0 and c isnot 0x0a)) or c is 0x7f) {
            ADD_INVERTED_CHAR ('?', HL_ERROR);
          }
          else {
            My(String).append_byte (s,  c);
          }

      if (++idx is len) goto theend;
      c = line[idx];
    }

    if (strchr ($my(syn)->operators, c) isnot NULL) {
      ADD_COLORED_CHAR (c, HL_OPERATOR);
      if (++idx is len) goto theend;
      c = line[idx];
      if (strchr ($my(syn)->operators, c) isnot NULL) { /* most likely one or two */
          ADD_COLORED_CHAR (c, HL_OPERATOR);
          goto thecontinue;
      }
      goto parse_char;
    }

    if (c is '"' or c is '\'') {
      ADD_COLORED_CHAR (c, HL_STR_DELIM);
      while (++idx < len) {
        c = line[idx];
        if (c is '"' or c is '\'') { /* handle case '"' "'" '\'' "\"" */
          ADD_COLORED_CHAR (c, HL_STR_DELIM);
          goto thecontinue;
        }

        My(String).append_byte (s,  c);
      }

      goto theend;
    }

    if (IS_DIGIT (c)) {
      ADD_COLORED_CHAR (c, HL_NUMBER);
      while (++idx < len) {
        c = line[idx];
        if (0 is IS_DIGIT (c) and 0 is IsAlsoANumber (c))
          goto parse_char; // most likely next loop is useless

        ADD_COLORED_CHAR (c, HL_NUMBER);
      }

      goto theend;
    }

    for (int j = 0; $my(syn)->keywords[j] isnot NULL; j++) {
      int kw_len = bytelen($my(syn)->keywords[j]);
      int kw_2 = $my(syn)->keywords[j][kw_len - 1] is '|';

      if (kw_2) kw_len--;

      if (0 == str_cmp_n (&line[idx], $my(syn)->keywords[j], kw_len) and
          IsSeparator (line[idx + kw_len])) {

        char kw[kw_len]; memcpy (kw, &line[idx], kw_len); kw[kw_len] = '\0';

        My(String).append_fmt (s, "%s%s%s",
          TERM_MAKE_COLOR (kw_2 ? HL_IDENTIFIER : HL_KEYWORD), kw, TERM_COLOR_RESET);

        idx += kw_len - 1;
        goto thecontinue;
      }
    }

    if (idx is m_cmnt_idx) {
      $my(syn)->state |= (SYN_HAS_OPEN_COMMENT|SYN_HAS_MULTILINE_COMMENT);
      goto open_comment;
    }
    if (idx is s_cmnt_idx) {
      $my(syn)->state |= (SYN_HAS_OPEN_COMMENT|SYN_HAS_SINGLELINE_COMMENT);
      goto open_comment;
    }

    My(String).append_byte (s,  c);

    while (++idx < len) {
      c = line[idx];
      if (IsSeparator (c))
        goto parse_char;

      My(String).append_byte (s,  c);
    }

thecontinue: {}
  }

theend:
  strncpy (line, s->bytes, s->num_bytes);
  line[s->num_bytes] = '\0';
  My(String).free (s);

  return line;
}

private string_t *buf_autoindent_default (buf_t *this, row_t *row) {
  (void) row;
  $my(shared_int) = 0;
  char s[$my(shared_int) + 1];
  for (int i = 0; i < $my(shared_int); i++) s[i] = ' ';
  s[$my(shared_int)] = '\0';
  My(String).replace_with ($my(shared_str), s);
  return $my(shared_str);
}

private string_t *buf_autoindent_c (buf_t *this, row_t *row) {
  do {
    if (row->data->num_bytes < 2 or row->data->bytes[0] is '}') {
      $my(shared_int) = 0;
      break;
    }

    int ws = 0;
    char *line = row->data->bytes;
    while (*line and *line++ is ' ') ws++;
    if (ws is (int) row->data->num_bytes) {
      $my(shared_int) = 0;
      break;
    }

    int len = row->data->num_bytes - ws - 2;
    if (len > 0) while (len--) line++;
    if (*line is '\n') line--;
    if (*line is ';' or *line is ',')
      $my(shared_int) = ws;
    else
      $my(shared_int) = ws + $my(ftype)->shiftwidth;
  } while (0);

  char s[$my(shared_int) + 1];
  for (int i = 0; i < $my(shared_int) ; i++) s[i] = ' ';
  s[$my(shared_int)] = '\0';
  My(String).replace_with ($my(shared_str), s);
  return $my(shared_str);
}

private ftype_t *buf_ftype_init_default (buf_t *this, int FTYPE,
string_t *(*indent_fun) (buf_t *, row_t *)) {
  $my(ftype) = AllocType (ftype);
  if (FTYPE >= (int) ARRLEN (HL_DB) or FTYPE < 0) FTYPE = FTYPE_DEFAULT;
  $my(syn) = &HL_DB[FTYPE];
  strcpy ($my(ftype)->name, $my(syn)->file_type);
  $my(ftype)->autochdir = 1;
  $my(ftype)->shiftwidth = 0;
  $my(ftype)->tab_indents = TAB_ON_INSERT_MODE_INDENTS;
  $my(ftype)->autoindent = indent_fun;
  strcpy ($my(ftype)->on_emptyline, "~");
  return $my(ftype);
}

private ftype_t *buf_ftype_init_c (buf_t *this) {
  ftype_t *ft = buf_ftype_init_default (this, FTYPE_C, buf_autoindent_c);
  ft->shiftwidth = 2;
  ft->tab_indents = 1;
  return ft;
}

private ftype_t *buf_default_ftype (buf_t *this) {
  return buf_ftype_init_default (this, FTYPE_DEFAULT, buf_autoindent_default);
}

private ftype_t *buf_set_ftype (buf_t *this) {
  if (NULL is $my(extname)) return buf_default_ftype (this);

  for (int i = 0; i < (int) ARRLEN(HL_DB); i++) {
    int j = 0;
    while (HL_DB[i].file_match[j])
      if (str_eq (HL_DB[i].file_match[j++], $my(extname)))
        return HL_DB[i].init (this);
  }

  return buf_default_ftype (this);
}

private row_t *buf_row_new_with (buf_t *this, const char *bytes) {
  row_t *row = AllocType (row);
  string_t *data = My(String).new_with (bytes);
  row->data = data;
  return row;
}

private void buf_free_row (buf_t *this, row_t *row) {
  if (row is NULL) return;
  My(String).free (row->data);
  free (row);
}

private void buf_free_ftype (buf_t *this) {
  if (this is NULL or $myprop is NULL or $my(ftype) is NULL) return;
  free ($my(ftype));
}

private void buf_free_action (buf_t *this, action_t *action) {
  (void) this;
  act_t *act = stack_pop (action, act_t);
  while (act isnot NULL) {
    free (act->bytes);
    free (act);
    act = stack_pop (action, act_t);
  }

  free (action);
}

private void buf_free_undo (buf_t *this) {
  action_t *action = vundo_pop (this);
  while (action isnot NULL) {
    buf_free_action (this, action);
    action = vundo_pop (this);
  }

  free ($my(undo));

  action = $my(redo)->head;
  while (action isnot NULL) {
    action_t *tmp = action->next;
    buf_free_action (this, action);
    action = tmp;
  }

  free ($my(redo));
}

private env_t *env_new () {
  env_t *env = AllocType (env);
  env->pid = getpid ();
  env->uid = getuid ();
  env->gid = getgid ();

  char *hdir = getenv ("HOME");
  if (NULL is hdir)
    env->home_dir = string_new ();
  else {
    env->home_dir = string_new_with (hdir);
    if (hdir[env->home_dir->num_bytes - 1] is PATH_SEP)
      string_set_nullbyte_at (env->home_dir, env->home_dir->num_bytes - 1);
  }

  return env;
}

private void env_free (env_t **env) {
  if (NULL is env) return;
  string_free ((*env)->home_dir);
  free (*env); *env = NULL;
}

private void history_free (hist_t **hist) {
  if (NULL is hist) return;
  h_search_t *hs = (*hist)->search;
  if (NULL isnot hs) {
    histitem_t *hitem = hs->head;

    while (hitem isnot NULL) {
      histitem_t *tmp = hitem->next;
      string_free (hitem->data);
      free (hitem);
      hitem = tmp;
    }
  }

  h_rline_t *hrl = (*hist)->rline;
  h_rlineitem_t *it = hrl->head;
  while (it isnot NULL) {
    h_rlineitem_t *tmp = it->next;
    rline_free (it->data);
    free (it);
    it = tmp;
  }

  free (hrl);
  free (hs); (*hist)->search = NULL;
  free (*hist); *hist = NULL;
}

private void buf_free_handler (buf_t *this) {
  if (this is NULL or $myprop is NULL) return;

  if ($my(fname) isnot NULL)
    free ($my(fname));

  free ($my(cwd));

  My(String).free ($my(statusline));
  My(String).free ($my(promptline));
  My(String).free ($my(shared_str));

  buf_free_ftype (this);
  buf_free_undo (this);

  free ($myprop);
}

private void buf_free (buf_t *this) {
  if (this is NULL) return;

  row_t *row = this->head;
  row_t *next;

  while (row isnot NULL) {
    next = row->next;
    buf_free_row (this, row);
    row = next;
  }

  buf_free_handler (this);

  free (this);
}

private void buf_set_video_first_row (buf_t *this, int idx) {
  if (idx >= this->num_items or idx < 0 or idx is $my(video_first_row_idx))
    return;

  if ($my(video_first_row_idx) < 0) {
    $my(video_first_row) = this->head;
    $my(video_first_row_idx) = 0;
  }

  int num;
  if (idx < $my(video_first_row_idx)) {
    num = $my(video_first_row_idx) - idx;
    loop (num) $my(video_first_row) = $my(video_first_row)->prev;
    $my(video_first_row_idx) -= num;
  } else {
    num = idx - $my(video_first_row_idx);
    loop (num) $my(video_first_row) = $my(video_first_row)->next;
    $my(video_first_row_idx) += num;
   }
}

private row_t *buf_current_prepend (buf_t *this, row_t *row) {
  return current_list_prepend (this, row);
}

private row_t *buf_current_append (buf_t *this, row_t *row) {
  return current_list_append (this, row);
}

private row_t *buf_append_with (buf_t *this, char *bytes) {
  row_t *row = self(row.new_with, bytes);
  int cur_idx = this->cur_idx;
  self(cur.set, this->num_items - 1);
  current_list_append (this, row);
  self(cur.set, cur_idx);
  return row;
}

private row_t *buf_current_prepend_with(buf_t *this, char *bytes) {
  row_t *row = self(row.new_with, bytes);
  return current_list_prepend (this, row);
}

private row_t *buf_current_append_with (buf_t *this, char *bytes) {
  row_t *row = self(row.new_with, bytes);
  return current_list_append (this, row);
}

private row_t *__buf_current_delete (buf_t *this, row_t **row) {
  if (this->current is NULL) return NULL;

  *row = this->current;

  if (this->num_items is 1) {
    this->current = NULL;
    this->head = NULL;
    this->tail = NULL;
    this->cur_idx = -1;
    goto theend;
  }

  if (this->cur_idx is 0) {
    this->current = this->current->next;
    this->current->prev = NULL;
    this->head = this->current;
    goto theend;
  }

  if (this->cur_idx + 1 is this->num_items) {
    this->cur_idx--;
    this->current = this->current->prev;
    this->current->next = NULL;
    this->tail = this->current;
    goto theend;
  }

  this->current->prev->next = this->current->next;
  this->current->next->prev = this->current->prev;
  this->current = this->current->next;

theend:
  this->num_items--;
  return this->current;
}

private row_t *buf_current_delete (buf_t *this) {
  row_t *row;
  __buf_current_delete (this, &row);

  if (row isnot NULL) self(free.row, row);

  return this->current;
}

private row_t *buf_current_pop (buf_t *this) {
  row_t *row;  __buf_current_delete (this, &row);
  return row;
}

private row_t *buf_current_pop_next (buf_t *this) {
  if (this->current->next is NULL) return NULL;

  this->current = this->current->next;
  this->cur_idx++;
  row_t *row; __buf_current_delete (this, &row);
  ifnot (this->cur_idx + 1 is this->num_items) {
    this->current = this->current->prev;
    this->cur_idx--;
  }

  return row;
}

private char *buf_get_fname (buf_t *this) {
  return $my(fname);
}

private int buf_current_set (buf_t *this, int idx) {
  return current_list_set (this, idx);
}

private int buf_set_fname (buf_t *this, char *fname) {
  if (fname is NULL or 0 is bytelen (fname) or str_eq (fname, UNAMED)) {
    strcpy ($my(fname), UNAMED);
    $my(basename) = $my(fname); $my(extname) = NULL;
    $my(flags) &= ~FILE_EXISTS;
    return OK;
  }

  int fname_exists = file_exists (fname);
  int is_abs = IS_PATH_ABS (fname);

  if (fname_exists) {
    if (is_directory (fname)) {
      VED_MSG_ERROR (MSG_FILE_EXISTS_AND_IS_A_DIRECTORY, fname);
      strcpy ($my(fname), UNAMED);
      $my(basename) = $my(fname);
      $my(flags) &= ~FILE_EXISTS;
      return NOTOK;
    }

    if (is_abs) {
      strncpy ($my(fname), fname, PATH_MAX);
    } else {
      char *cwd = dir_get_current ();
      size_t len = bytelen (cwd) + bytelen (fname) + 2;
      char tmp[len]; snprintf (tmp, len, "%s/%s", cwd, fname);
      /* $my(fname) = realpath (tmp, NULL); aborts with invalid argument on tcc */
      realpath (tmp, $my(fname));
      free (cwd);
    }

    $my(flags) |= FILE_EXISTS;
  } else {
    if (is_abs) {
      strncpy ($my(fname), fname, PATH_MAX);
    } else {
      char *cwd = dir_get_current ();
      snprintf ($my(fname), PATH_MAX + 1, "%s/%s", cwd, fname);
      free (cwd);
    }

    $my(flags) &= ~FILE_EXISTS;
  }

  buf_t *buf = My(Ed).get.bufname ($my(root), $my(fname));
  if (buf isnot NULL) {
    VED_MSG_ERROR (MSG_FILE_IS_LOADED_IN_ANOTHER_BUFFER, fname);
    $my(flags) |= BUF_IS_RDONLY;
  }

  $my(basename) = path_basename ($my(fname));
  $my(extname) = path_extname ($my(fname));
  return OK;
}

private ssize_t buf_read_fname (buf_t *this) {
  if ($my(fname) is NULL or str_eq ($my(fname), UNAMED)) return NOTOK;

  FILE *fp = fopen ($my(fname), "r");

  if (fp is NULL) {
    if (EACCES is errno) {
      if (file_exists ($my(fname))) {
        $my(flags) |= FILE_EXISTS;
        $my(flags) &= ~(FILE_IS_READABLE|FILE_IS_WRITABLE);
        VED_MSG_ERROR(MSG_FILE_IS_NOT_READABLE, $my(fname));
        return NOTOK;
      } else
       return OK;

    } else {
      SYS_MSG_ERROR(errno);
      return NOTOK;
    }
  }

  fstat (fileno (fp), &$my(st));
  $my(flags) |= (FILE_EXISTS|FILE_IS_READABLE);

  if (OK is access ($my(fname), W_OK))
    $my(flags) &= ~FILE_IS_WRITABLE;
  else
    $my(flags) |= FILE_IS_WRITABLE;

  $my(flags) |= S_ISREG ($my(st).st_mode);

  size_t t_len = 0;

  ifnot ($my(flags) & FILE_IS_REGULAR) {
    t_len = NOTOK;
    goto theend;
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t nread;

  while (-1 != (nread = getline (&line, &len, fp))) {
    if (nread and (line[nread - 1] is '\n' or line[nread - 1] is '\r'))
      line[nread - 1] = '\0';

    buf_current_append_with (this, line);
    t_len += nread;
  }

  if (line isnot NULL) free (line);

theend:
  fclose (fp);
  return t_len;
}

private void buf_on_no_length (buf_t *this) {
  buf_current_append_with (this, " ");
  My(String).clear_at (this->current->data, 0);
}

private void win_adjust_buf_dim (win_t *w) {
  buf_t *this = w->head;
  while (this) {
    $my(dim) = $from(w)->frames_dim[$my(at_frame)];
    $my(statusline_row) = $my(dim)->last_row;
    $my(video_first_row_idx) = this->cur_idx;
    $my(video_first_row) = this->current;
    $my(cur_video_row) = $my(dim)->first_row;
    for (int i = 0; i < 4 and $my(video_first_row_idx) and
        i < $my(dim)->num_rows - 1; i++) {
      $my(video_first_row_idx)--; $my(video_first_row) = $my(video_first_row)->prev;
      $my(cur_video_row)++;
    }

    this = this->next;
  }
}

private void win_set_video_dividers (win_t *this) {
  ifnot ($my(has_dividers)) return;

  int len = $my(dim)->num_cols + TERM_SET_COLOR_FMT_LEN + TERM_COLOR_RESET_LEN;
  char line[len + 1];
  snprintf (line, TERM_SET_COLOR_FMT_LEN + 1, TERM_SET_COLOR_FMT, COLOR_DIVIDER);
  for (int i = 0; i < $my(dim)->num_cols; i++)
    line[i + TERM_SET_COLOR_FMT_LEN] = '_';

  snprintf (line + TERM_SET_COLOR_FMT_LEN + $my(dim)->num_cols,
    len, "%s", TERM_COLOR_RESET);

  line[len+1] = '\0';
   for (int i = 0; i < $my(num_frames) - 1; i++) {
    My(Video).set_with ($my(video), $my(frames_dim)[i]->last_row,
       line);
  }
}

private int win_add_frame (win_t *this) {
  if ($my(num_frames) is $my(max_frames)) return NOTHING_TODO;

  for (int i = 0; i < $my(num_frames); i++) free ($my(frames_dim)[i]);
  free ($my(frames_dim));

  $my(num_frames)++;
  $my(frames_dim) = dim_calc (this, $my(dim)->num_rows, $my(num_frames),
      $my(min_rows), $my(has_dividers));

  self(adjust.buf_dim);
  self(set.video_dividers);
  return DONE;
}

private int win_delete_frame (win_t *this, int idx) {
  if ($my(num_frames) is 1) return NOTHING_TODO;

  for (int i = 0; i < $my(num_frames); i++) free ($my(frames_dim)[i]);
  free ($my(frames_dim));

  $my(num_frames)--;
  $my(frames_dim) = dim_calc (this, $my(dim)->num_rows, $my(num_frames),
      $my(min_rows), $my(has_dividers));

  buf_t *it = this->head;
  while (it isnot NULL) {
    if (it->prop->at_frame > $my(num_frames) - 1)
        it->prop->at_frame = $my(num_frames) - 1;
    else
      if (it->prop->at_frame >= idx)
        it->prop->at_frame--;

    it = it->next;
  }

  if ($my(cur_frame) >= $my(num_frames))
    $my(cur_frame) = $my(num_frames) - 1;

  self(adjust.buf_dim);
  self(set.video_dividers);
  return DONE;
}

private buf_t *win_change_frame (win_t* w, int frame) {
  if (frame < 0 or frame > $from(w)->num_frames - 1) return NULL;
  int idx = 0;
  buf_t *this = w->head;
  while (this isnot NULL) {
    if ($my(at_frame) is frame and $my(is_visible)) {
      My(Win).set.current_buf ($my(parent), idx);
      $from(w)->cur_frame = frame;
      return w->current;
    }

    idx++;
    this = this->next;
  }

  return NULL;
}

private buf_t *win_buf_new (win_t *w, char *fname, int frame) {
  buf_t *this = AllocType (buf);
  $myprop = AllocProp (buf);

  $my(parent) = w;
  $my(root) = $myparents(parent);

  $my(Ed) = $myparents(Ed);
  $my(Me) = $myparents(Buf);
  $my(Win)= $myparents(Me);
  $my(Cstring) = $myparents(Cstring);
  $my(String) = $myparents(String);
  $my(Re) = $myparents(Re);
  $my(Msg) = $myparents(Msg);
  $my(Error) = $myparents(Error);
  $my(Video) = $myparents(Video);
  $my(Term) = $myparents(Term);
  $my(Input) = $myparents(Input);
  $my(Cursor) = $myparents(Cursor);
  $my(Screen)= $myparents(Screen);

  strcpy ($my(mode), NORMAL_MODE);

  $my(term_ptr) = $myroots(term);
  $my(msg_row_ptr) = &$myroots(msg_row);
  $my(prompt_row_ptr) = &$myroots(prompt_row);

  $my(regs) = &$myroots(regs)[0];
  $my(video) = $myroots(video);

  $my(undo) = AllocType (undo);
  $my(redo) = AllocType (undo);

  $my(history) = $myroots(history);
  $my(last_insert) = $myroots(last_insert);

  $my(at_frame) = frame;
  $my(dim) = $myparents(frames_dim)[$my(at_frame)];

  $my(statusline_row) = $my(dim->last_row);
  $my(statusline) = My(String).new ();
  $my(promptline) = My(String).new ();

  $my(shared_str) = My(String).new ();

  $my(is_visible) = 0;
  $my(flags) &= ~BUF_IS_MODIFIED;
  $my(fname) = Alloc (PATH_MAX + 1);

  self(set.fname, fname);

  if ($my(flags) & FILE_EXISTS) {
    self(read.fname);

    ifnot (this->num_items)
      buf_on_no_length (this);
  } else
    buf_on_no_length (this);

  for (int i = 0; i < NUM_MARKS; i++)
    $my(marks)[i] = (mark_t) {.mark = MARKS[i], .video_first_row = NULL};

  self(cur.set, 0);

  $my(video_first_row_idx) = 0;
  $my(video_first_row) = this->head;
  $my(video)->row_pos = $my(cur_video_row) = $my(dim)->first_row;
  $my(video)->col_pos = $my(cur_video_col) = 1;

  $my(ftype) = self(set.ftype);
  $my(cwd) = str_eq ($my(fname), UNAMED) ? dir_get_current () : ved_dirname ($my(fname));
  this->free = buf_free_handler;
  return this;
}

private buf_t *win_buf_new_special (win_t *w, char *name) {
  buf_t *this = win_buf_new (w, name, 0);
  $my(flags) |= (BUF_IS_RDONLY|BUF_IS_PAGER);
  My(Win).append_buf (w, this);
  return this;
}

private buf_t *win_set_current_buf (win_t *w, int idx) {
  buf_t *this = w->current;

  int cur_idx = w->cur_idx;
  int cur_frame = $from(w)->cur_frame;

  if (idx is cur_idx) goto change;

  int *is_visible = &$my(is_visible);

  int lidx = current_list_set (w, idx);

  if (lidx is INDEX_ERROR) return NULL;

  if (cur_idx isnot lidx)
    w->prev_idx = cur_idx;

  this = w->current;

  if (cur_frame is $my(at_frame))
    *is_visible = 0;
  else
    $from(w)->cur_frame = $my(at_frame);

change:
  $my(is_visible) = 1;
  $my(video)->row_pos = $my(cur_video_row);
  $my(video)->col_pos = $my(cur_video_col);

  if ($my(ftype)->autochdir)
    if (-1 is chdir ($my(cwd)))
      SYS_MSG_ERROR(errno);

  if ($my(flags) & FILE_EXISTS) {
    struct stat st;
    if (-1 is stat ($my(fname), &st)) {
      VED_MSG_ERROR(MSG_FILE_REMOVED_FROM_FILESYSTEM);
    } else {
      if ($my(st).st_mtim.tv_sec isnot st.st_mtim.tv_sec)
        VED_MSG_ERROR(MSG_FILE_HAS_BEEN_MODIFIED);
    }
  }

  self(draw);

  return this;
}

private buf_t *win_get_current_buf (win_t *w) {
  return w->current;
}

private buf_t *win_get_buf_by_idx (win_t *w, int idx) {
  if (w is NULL) return NULL;

  if (0 > idx) idx += w->num_items;

  if (idx < 0 || idx >= w->num_items) return NULL;

  buf_t *this = w->head;
  loop (idx) this = this->next;

  return this;
}

private buf_t *win_get_buf_by_fname (win_t *w, const char *fname, int *idx) {
  if (w is NULL or fname is NULL)
    return NULL;

 *idx = 0;
  buf_t *this = w->head;
  while (this) {
    if ($my(fname) isnot NULL)
      if (str_eq ($my(fname), fname))
        return this;

    this = this->next;
    *idx += 1;
  }

  return NULL;
}

private int win_append_buf (win_t *this, buf_t *buf) {
  current_list_append (this, buf);
  return this->cur_idx;
}

private int win_get_num_buf (win_t *w) {
  return w->num_items;
}

private void win_draw (win_t *w) {
  if (w->num_items is 0) return;

  buf_t *this = w->head;
  while (this) {
    if ($my(is_visible)) {
      if (this is w->current)
        My(Ed).set.topline (this);

      self(to.video);
    }
    this = this->next;
  }

  this = w->head;
  My(Video).Draw.all ($my(video));
}

private void win_free (win_t *this) {
  buf_t *buf = this->head;
  buf_t *next;

  while (buf isnot NULL) {
    next = buf->next;
    buf_free (buf);
    buf = next;
  }

  if ($myprop isnot NULL) {
    free ($my(name));

    if ($my(dim) isnot NULL)
      free ($my(dim));

    if ($my(frames_dim) isnot NULL) {
      for (int i = 0; i < $my(num_frames); i++)  free ($my(frames_dim)[i]);

      free ($my(frames_dim));
    }

    free ($myprop);
  }

 free (this);
}

private win_t *ed_win_new (ed_t *ed, char *name, int num_frames) {
  win_t *this = AllocType (win);
  $myprop = AllocProp (win);
  if (NULL is name) {
    int num = ed->name_gen / 26;
    $my(name) = Alloc (num * sizeof (char *) + 1);
    for (int i = 0; i < num; i++) $my(name)[i] = 'a' + (ed->name_gen++ ^ 26);
    $my(name)[num] = '\0';
  } else
    $my(name) = str_dup (name, bytelen (name));

  $my(parent) = ed;

  $my(Ed) = $myparents(Me);
  $my(Me) = &$myparents(Me)->Win;
  $my(Buf) = &$myparents(Me)->Buf;
  $my(Cstring) = $myparents(Cstring);
  $my(String) =$myparents(String);
  $my(Re) = $myparents(Re);
  $my(Msg) = $myparents(Msg);
  $my(Error) = $myparents(Error);
  $my(Video) = $myparents(Video);
  $my(Term) = $myparents(Term);
  $my(Input) = $myparents(Input);
  $my(Cursor) = $myparents(Cursor);
  $my(Screen)= $myparents(Screen);

  $my(video) = $myparents(video);
  $my(min_rows) = 4;
  $my(has_topline) = $myparents(has_topline);
  $my(has_msgline) = $myparents(has_msgline);
  $my(has_promptline) = $myparents(has_promptline);
  $my(dim) = dim_new (
      $myparents(dim->first_row),
      $myparents(dim->num_rows),
      $myparents(dim->first_col),
      $myparents(dim->num_cols));

  $my(has_dividers) = 1;
  self(set.video_dividers);

  $my(cur_frame) = 0;
  $my(num_frames) = num_frames;
  $my(max_frames) = MAX_FRAMES;

  $my(frames_dim) = dim_calc (this, $my(dim)->num_rows, $my(num_frames),
      $my(min_rows), $my(has_dividers));

  return this;
}

private void ed_win_readjust_size (ed_t *ed, win_t *this) {
  (void) ed;
  ifnot (NULL is $my(dim)) {
    free ($my(dim));
    $my(dim) = NULL;
  }

  $my(dim) = dim_new (
    $myparents(dim)->first_row, $myparents(dim)->num_rows,
    $myparents(dim)->first_col, $myparents(dim)->num_cols);

  ifnot (NULL is $my(frames_dim)) {
    for (int i = 0; i < $my(num_frames); i++) free ($my(frames_dim)[i]);
    free ($my(frames_dim));
    $my(frames_dim) = NULL;
  }

  $my(frames_dim) = dim_calc (this, $my(dim)->num_rows, $my(num_frames),
      $my(min_rows), $my(has_dividers));
  self(adjust.buf_dim);
  self(set.video_dividers);
  $my(video)->row_pos = this->current->prop->cur_video_row;
  $my(video)->col_pos = this->current->prop->cur_video_col;
  if (this is $my(parent)->current) {
    My(Video).set_with ($my(video), $myparents(prompt_row), " ");
    My(Video).set_with ($my(video), $myparents(msg_row), " ");
    self(draw);
  }
}

private void ed_check_msg_status (buf_t *this) {
  if ($myroots(msg_send) is 1)
      $myroots(msg_send)++;
  else if (2 is $myroots(msg_send)) {
    My(Video).set_with ($my(video), *$my(msg_row_ptr) - 1, " ");
    My(Video).Draw.row_at ($my(video), *$my(msg_row_ptr));
    $myroots(msg_send) = 0;
  }
}

private int ed_msg_buf (buf_t **thisp) {
  return ved_win_change (thisp, 0, VED_SPECIAL_WIN, 1);
}

private void ed_append_message (ed_t *this, char *msg) {
  My(Buf).append_with (this->head->head, msg);
}

private char *ed_msg_fmt (ed_t *this, int msgid, ...) {
  char fmt[MAXERRLEN]; fmt[0] = '\0';
  char pat[16]; snprintf (pat, 16, "%d:", msgid);
  char *sp = strstr (ED_MSGS_FMT, pat);
  if (sp isnot NULL) {
    int i;
    for (i = 0; i < (int) bytelen (pat); i++) sp++;
    for (i = 0; *sp and *sp isnot '.'; sp++) fmt[i++] = *sp;
    fmt[i] = '\0';
  }
  char bytes[MAXLINE];
  va_list ap;
  va_start(ap, msgid);
  vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  My(String).replace_with ($my(shared_str), bytes);
  return $my(shared_str)->bytes;
}

private void ed_msg_send (ed_t *this, int color, char *msg) {
  self(append.message, msg);
  $my(msg_send) = 1;
  My(String).replace_with_fmt ($my(msgline), TERM_SET_COLOR_FMT, color);
  int numchars = 0; int clen;
  for (int idx = 0; numchars < $my(dim)->num_cols and msg[idx]; idx++) {
    clen = char_byte_len (msg[idx]);
    for (int i = 0; i < clen; i++) My(String).append_byte ($my(msgline), msg[idx + i]);
    numchars++;  idx += clen - 1;
  }

  My(String).append($my(msgline), TERM_COLOR_RESET);
  My(Video).set_with ($my(video), $my(msg_row) - 1, $my(msgline)->bytes);
  My(Video).Draw.row_at ($my(video), $my(msg_row));
}

private void ed_msg_error (ed_t *this, char *msg) {
  My(Msg).send (this, COLOR_FAILURE, msg);
}

private void ed_msg_error_fmt (ed_t *this, char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  My(Msg).send (this, COLOR_FAILURE, bytes);
}

private void ed_msg_send_fmt (ed_t *this, int color, char *fmt, ...) {
  char bytes[(VA_ARGS_FMT_SIZE) + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf (bytes, sizeof (bytes), fmt, ap);
  va_end(ap);
  My(Msg).send (this, color, bytes);
}

private char *ed_error_string (ed_t *this, int err) {
  char ebuf[MAXERRLEN];
  ebuf[0] = '\0';
  char epat[16];
  snprintf (epat, 16, "%d:", err);
  char *sp = strstr (SYS_ERRORS, epat);
  if (sp is NULL) {
    snprintf (epat, 16, "%d:",  err);
    sp = strstr (ED_ERRORS, epat);
  }

  if (sp is NULL) return NULL;
  int i;
  for (i = 0; *sp is ' ' or i <= (int) bytelen (epat); i++) sp++;
  for (i = 0; *sp and *sp isnot ' '; sp++) ebuf[i++] = *sp;
  for (; *sp and *sp is ' '; sp++);
  ebuf[i++] = ':'; ebuf[i++] = ' ';
  for (; *sp and *sp isnot ','; sp++) ebuf[i++] = *sp;
  ebuf[i] = '\0';

  My(String).replace_with ($my(shared_str), ebuf);
  return $my(shared_str)->bytes;
}

private void buf_set_topline (buf_t *this) {
  time_t tim = time (NULL);
  struct tm *tm = localtime (&tim);

  My(String).replace_with_fmt ($myroots(topline), "[%s] ftype (%s) [pid %d]",
    $my(mode), $my(ftype)->name, $myroots(env)->pid);

  char tmnow[32];
  strftime (tmnow, sizeof (tmnow), "[%a %d %H:%M:%S]", tm);
  int pad = $my(dim->num_cols) - $myroots(topline)->num_bytes - bytelen (tmnow);
  if (pad > 0)
    loop (pad) My(String).append ($myroots(topline), " ");

  My(String).append_fmt ($myroots(topline), "%s%s", tmnow, TERM_COLOR_RESET);
  My(String).prepend_fmt ($myroots(topline), TERM_SET_COLOR_FMT, COLOR_TOPLINE);
  My(Video).set_with ($my(video), 0, $myroots(topline)->bytes);
}

private void buf_set_draw_topline (buf_t *this) {
  My(Ed).set.topline (this);
  My(Video).Draw.row_at ($my(video), 1);
}

private void buf_set_statusline (buf_t *this) {
  if ($my(dim->num_rows) is 1) return;

  My(String).replace_with_fmt ($my(statusline),
    TERM_SET_COLOR_FMT "%s (line: %d/%d idx: %d len: %d chr: %d) [fci %d vrp %d vcp %d vfr %d curidx %d] %d",
    COLOR_STATUSLINE,
    $my(basename), this->cur_idx + 1, this->num_items, $mycur(cur_col_idx),
    $mycur(data)->num_bytes, cur_utf8_code, $mycur(first_col_idx), $my(cur_video_row),
    $my(cur_video_col), $my(video_first_row_idx), this->cur_idx, $my(video)->row_pos);

  My(String).clear_at ($my(statusline), $my(dim)->num_cols + TERM_SET_COLOR_FMT_LEN);
  My(String).append_fmt ($my(statusline), "%s", TERM_COLOR_RESET);
  My(Video).set_with ($my(video), $my(statusline_row) - 1, $my(statusline)->bytes);
}

private void buf_set_draw_statusline (buf_t *this) {
  buf_set_statusline (this);
  My(Video).Draw.row_at ($my(video), $my(statusline_row));
}

private int buf_adjust_col (buf_t *this, int previdx, int isatend) {
  if (this->current is NULL) return 1;
  if (0 is previdx or $mycur(data)->num_bytes is 0 or
      (int) $mycur(data)->num_bytes is char_byte_len ($mycur(data)->bytes[0])) {

    $mycur(cur_col_idx) = $mycur(first_col_idx) = 0;

    $my(video)->col_pos = $my(cur_video_col) = 1;
    return $my(video)->col_pos;
  }

  int clen = 0;
  if (isatend) {
    char s[$mycur(data)->num_bytes];
    string_reverse_from_to (s, $mycur(data)->bytes, 0, $mycur(data)->num_bytes - 1);

    clen = char_byte_len (*s);
    $mycur(cur_col_idx) = $mycur(data)->num_bytes - clen;
  } else {
    int idx = 0;
    char *s = $mycur(data)->bytes;
    while (*s and (idx < (int) $mycur(data)->num_bytes and idx < previdx)) {
      clen = char_byte_len (*s);
      idx += clen; s += clen;
    }

    if (idx >= (int) $mycur(data)->num_bytes)
      $mycur(cur_col_idx) = $mycur(data)->num_bytes - clen;
    else
      $mycur(cur_col_idx) = idx - (idx < previdx ? clen : 0);
  }

  int col_pos = 1; /* cur_col_idx char */;
  int idx = $mycur(cur_col_idx);
  int i = 0;

  char s[$mycur(cur_col_idx) + clen + 1];
  string_reverse_from_to (s, $mycur(data)->bytes, 0, $mycur(cur_col_idx) - 1);

  while (idx and (col_pos < $my(dim)->num_cols)) {
    int len = char_byte_len (s[i]);
    i += len;
    idx -= len;
    col_pos++;
  }

  $mycur(first_col_idx) = idx;
  $my(video)->col_pos = $my(cur_video_col) = col_pos;

  return $my(video)->col_pos;
}

private void buf_adjust_marks (buf_t *this, int type, int fidx, int lidx) {
  for (int m = 0; m < NUM_MARKS; m++) {
    mark_t *mark = &$my(marks)[m];
    if (mark->video_first_row is NULL) continue;
    if (mark->cur_idx < fidx) continue;
    if (fidx <= mark->cur_idx and mark->cur_idx <= lidx) {
      mark->video_first_row = NULL;
      continue;
    }

    if (type is DELETE_LINE)
      if (fidx is 0 and mark->video_first_row_idx is 0)
        mark->video_first_row = this->head;

    int lcount = lidx - fidx + (type is DELETE_LINE);
    int idx = this->cur_idx;

    mark->video_first_row_idx = idx;
    mark->video_first_row = this->current;

    if (type is DELETE_LINE)
      mark->cur_idx -= lcount;
    else
      mark->cur_idx += lcount;

    while (idx++ < mark->cur_idx) {
      mark->video_first_row_idx++;
      mark->video_first_row = mark->video_first_row->next;
    }

    mark->row_pos = $my(dim)->first_row;

    idx = 5 < $my(dim)->num_rows ? 5 : $my(dim)->num_rows;
    while (mark->video_first_row_idx > 0 and idx--) {
      mark->video_first_row_idx--;
      mark->video_first_row = mark->video_first_row->prev;
      mark->row_pos++;
    }
  }
}

private void buf_adjust_view (buf_t *this) {
  $my(video_first_row) = this->current;
  $my(video_first_row_idx) = this->cur_idx;
  int num = (ONE_PAGE / 2);

  while ($my(video_first_row_idx) and num) {
    $my(video_first_row_idx)--;
    num--;
    $my(video_first_row) = $my(video_first_row)->prev;
  }

  $mycur(first_col_idx) = $mycur(cur_col_idx) = 0;

  $my(video)->row_pos = $my(cur_video_row) =
      $my(dim)->first_row + ((ONE_PAGE / 2) - num);
  $my(video)->col_pos = $my(cur_video_col) = 1;
}

private string_t *get_current_number (buf_t *this, int *fidx) {
  if ($mycur(data)->num_bytes is 0) return NULL;

  string_t *nb = string_new ();
  int type = 'd';
  int issign = 0;

  int idx = $mycur(cur_col_idx);
  uchar c;

  while (idx >= 0) {
    c = $mycur(data)->bytes[idx--];

    if (type is 'x') {
      if ('0' isnot c) goto theerror;
      string_prepend_byte (nb, c);
      *fidx = idx + 1;
      break;
    }

    if (IS_DIGIT (c) or IsAlsoAHex (c)) {
      string_prepend_byte (nb, c);
      continue;
    } else {
      if (c is 'x') {
       if (type is 'x') goto theerror;
       type = 'x';
       string_prepend_byte (nb, c);
       continue;
      }

      if (c is '-') { issign = 1; idx--; }
      *fidx = idx + 2;
      break;
    }
  }

  idx = $mycur(cur_col_idx) + 1;
  while (idx < (int) $mycur(data)->num_bytes) {
    c = $mycur(data)->bytes[idx++];

    if (IS_DIGIT (c) or IsAlsoAHex (c)) {
      string_append_byte (nb, c);
      continue;
    }

    if (c is 'x') {
      if (type is 'x') goto theerror;
      if (nb->num_bytes isnot 1 and nb->bytes[0] isnot '0') goto theerror;
      string_append_byte (nb, c);
      type = 'x';
      continue;
    }

    break;
  }

  if (nb->num_bytes is 0) goto theerror;

  if (type is 'x')  goto theend;

  idx = 0;
  if (nb->bytes[idx] is '0' and nb->num_bytes > 1) {
    while (idx < (int) nb->num_bytes - 1)
      if (nb->bytes[idx] < '0' or nb->bytes[idx++] > '8') goto theerror;

    type = 'o';
    goto theend;
  }

  goto theend;

theerror:
  string_free (nb);
  return NULL;

theend:
  if (issign) string_prepend_byte (nb, '-');
  string_prepend_byte (nb, type);
  return nb;
}

private char *get_current_word (buf_t *this, char *word, char *Nwtype, int len) {
  int idx = $mycur(cur_col_idx);

  if (IS_SPACE ($mycur(data)->bytes[idx]) or
      IS_CNTRL ($mycur(data)->bytes[idx]) or
      NULL isnot memchr (Nwtype, $mycur(data)->bytes[idx], len))
    return NULL;

  while (idx > 0 and
         IS_SPACE ($mycur(data)->bytes[idx]) is 0 and
         IS_CNTRL ($mycur(data)->bytes[idx]) is 0 and
         NULL is memchr (Nwtype, $mycur(data)->bytes[idx], len))
    idx--;

  if (idx isnot 0 or (
      IS_SPACE ($mycur(data)->bytes[idx]) or
      IS_CNTRL ($mycur(data)->bytes[idx]) or
      NULL isnot memchr (Nwtype, $mycur(data)->bytes[idx], len)))
    idx++;

  int widx = 0;
  while (idx < (int) $mycur(data)->num_bytes and
      IS_SPACE ($mycur(data)->bytes[idx]) is 0 and
      IS_CNTRL ($mycur(data)->bytes[idx]) is 0 and
      NULL is memchr (Nwtype, $mycur(data)->bytes[idx], len))
    word[widx++] = $mycur(data)->bytes[idx++];

  word[widx] = '\0';

  return word;
}

#define SEARCH_UPDATE_ROW(idx)                    \
({                                                \
  int idx__ = (idx);                              \
  if (sch->dir is -1) {                           \
    if (idx__ is 0) {                             \
      idx__ = this->num_items - 1;                \
      sch->row = this->tail;                      \
    } else {                                      \
      idx__--;                                    \
      sch->row = sch->row->prev;                  \
    }                                             \
  } else {                                        \
    if (idx__ is this->num_items - 1) {           \
      idx__ = 0;                                  \
      sch->row = this->head;                      \
    } else {                                      \
      idx__++;                                    \
      sch->row = sch->row->next;                  \
    }                                             \
  }                                               \
                                                  \
  idx__;                                          \
})

#define SEARCH_FREE                               \
({                                                \
  My(String).free (sch->pat);                     \
  stack_free (sch, sch_t);                        \
  ifnot (NULL is sch->prefix) free (sch->prefix); \
  ifnot (NULL is sch->match) free (sch->match);   \
  free (sch);                                     \
})

#define SEARCH_PUSH(idx_, row_)                   \
  sch_t *s_ = AllocType (sch);                    \
  s_->idx = (idx_);                               \
  s_->row  = (row_);                              \
  stack_push (sch, s_)

private int __ved_search__ (buf_t *this, search_t *sch) {
  int retval = NOTOK;
  int idx = sch->cur_idx;
  int flags = 0;
  regexp_t *re = My(Re).new (sch->pat->bytes, flags, 9, My(Re).compile);

  sch->found = 0;
  SEARCH_PUSH (sch->cur_idx, sch->row);

  do {
    int ret = My(Re).exec (re, sch->row->data->bytes, sch->row->data->num_bytes);
    if (ret is RE_UNBALANCED_BRACKETS_ERROR) {
      VED_MSG_ERROR (RE_UNBALANCED_BRACKETS_ERROR);
      break;
    }

    if (0 <= ret) {
      sch->idx = idx;
      sch->found = 1;
      sch->col = re->match_idx;
      sch->match = Alloc ((sizeof (char) * re->match_len) + 1);
      memcpy (sch->match, re->match_ptr, re->match_len);
      sch->match[re->match_len] = '\0';

      sch->prefix = Alloc ((sizeof (char) * sch->col) + 1);
      memcpy (sch->prefix, sch->row->data->bytes, sch->col);
      sch->prefix[sch->col] = '\0';
      sch->end = sch->row->data->bytes + re->retval;
      retval = OK;
      goto theend;
    }

    idx = SEARCH_UPDATE_ROW (idx);

  } while (idx isnot sch->cur_idx);

theend:
  My(Re).free (re);
  return retval;
}

private int rline_search_at_beg (rline_t **rl) {
  switch ((*rl)->c) {
    case ESCAPE_KEY:
    case '\r':
    case CTRL('n'):
    case CTRL('p'):
    case ARROW_UP_KEY:
    case ARROW_DOWN_KEY:

    case '\t':
    (*rl)->state |= RL_POST_PROCESS;
    return RL_POST_PROCESS;
  }

  (*rl)->state |= RL_OK;
  return RL_OK;
}

private int ved_search (buf_t *this, char com) {
  if (this->num_items is 0) return NOTHING_TODO;

  int toggle = 0;

  search_t *sch = AllocType (search);
  sch->found = 0;
  sch->prefix = sch->match = NULL;
  sch->row = this->current;

  if (com is '/' or com is '*' or com is 'n') sch->dir = 1;
  else sch->dir = -1;

  sch->cur_idx = SEARCH_UPDATE_ROW (this->cur_idx);

  histitem_t *his = $my(history)->search->head;

  VED_MSG(" ");

  rline_t *rl = rline_new ($my(root), $my(term_ptr), My(Input).get, *$my(prompt_row_ptr),
    $my(dim)->num_cols, $my(video));
  rl->at_beg = rline_search_at_beg;
  rl->at_end = rline_break;

  rl->prompt_char = (com is '*' or com is '/') ? '/' : '?';

  if (com is '*' or com is '#') {
    com = '*' is com ? '/' : '?';

    char word[MAXLINE];
    get_current_word (this, word, Notword, Notword_len);
    sch->pat = My(String).new_with (word);
    if (sch->pat->num_bytes) {
      BYTES_TO_RLINE (rl, sch->pat->bytes, (int) sch->pat->num_bytes);
      rl->state |= RL_WRITE|RL_BREAK;
      rline_edit (rl);
      goto search;
    }
  } else {
    if (com is 'n' or com is 'N') {
      if ($my(history)->search->num_items is 0) return NOTHING_TODO;
      sch->pat = My(String).new_with (his->data->bytes);
      BYTES_TO_RLINE (rl, sch->pat->bytes, (int) sch->pat->num_bytes);

      com = 'n' is com ? '/' : '?';
      rl->prompt_char = com;
      rl->state |= RL_WRITE|RL_BREAK;
      rline_edit (rl);
      goto search;
    } else
      sch->pat = My(String).new ();
  }

  for (;;) {
    utf8 c = rline_edit (rl)->c;
    string_t *p = vstr_join (rl->line, "");
    if (rl->line->tail->data->bytes[0] is ' ')
      string_set_nullbyte_at (p, p->num_bytes - 1);

    if (str_eq (sch->pat->bytes, p->bytes)) {
      string_free (p);
    } else {
      string_replace_with (sch->pat, p->bytes);
      string_free (p);

search:
      ifnot (NULL is sch->prefix) { free (sch->prefix); sch->prefix = NULL; }
      ifnot (NULL is sch->match) { free (sch->match); sch->match = NULL; }
      __ved_search__ (this, sch);
      if (toggle) {
        sch->dir = (sch->dir is 1) ? -1 : 1;
        toggle = 0;
      }

      if (sch->found) {
        VED_MSG("|%d %d|%s%s%s%s%s", sch->idx + 1, sch->col, sch->prefix,
            TERM_MAKE_COLOR(COLOR_RED), sch->match, TERM_COLOR_RESET, sch->end);
        sch->cur_idx = sch->idx;
      } else {
        sch_t *s = stack_pop (sch, sch_t);
        if (NULL isnot s) {
          sch->cur_idx = s->idx;
          sch->row = s->row;
          free (s);
        }

      VED_MSG(" ");
      }

      continue;
    }

    switch (c) {

      case ESCAPE_KEY:
        sch->found = 0;

      case '\r':
        goto theend;

      case CTRL('n'):
        if (sch->found) {
          sch->cur_idx = SEARCH_UPDATE_ROW (sch->idx);
          goto search;
        }

        continue;

      case CTRL('p'):
        if (sch->found) {
          sch->dir = (sch->dir is 1) ? -1 : 1;
          sch->cur_idx = SEARCH_UPDATE_ROW (sch->idx);
          toggle = 1;
          goto search;
        }

        continue;

      case ARROW_UP_KEY:
      case ARROW_DOWN_KEY:
        if ($my(history)->search->num_items is 0) continue;
        if (c is ARROW_DOWN_KEY) {
          if (his->prev isnot NULL)
            his = his->prev;
        } else {
          if (his->next isnot NULL)
            his = his->next;
        }

        My(String).replace_with (sch->pat, his->data->bytes);

        rl->state |= RL_CLEAR_FREE_LINE;
        rline_clear (rl);
        BYTES_TO_RLINE (rl, sch->pat->bytes, (int) sch->pat->num_bytes);
        goto search;

      default:
        continue;
    }
  }

theend:
  if (sch->found) {
    histitem_t *h = AllocType (histitem);
    h->data = string_new_with (sch->pat->bytes);
    list_push ($my(history)->search, h);
    ved_normal_goto_linenr (this, sch->idx + 1);
  }

  VED_MSG(" ");

  rline_clear (rl);
  rline_free (rl);

  SEARCH_FREE;
  return DONE;
}

#define state_set(v__)                                   \
  (v__)->video_first_row = $my(video_first_row);         \
  (v__)->video_first_row_idx = $my(video_first_row_idx); \
  (v__)->row_pos = $my(video)->row_pos;                  \
  (v__)->col_pos = $my(video)->col_pos;                  \
  (v__)->cur_col_idx = $mycur(cur_col_idx);              \
  (v__)->first_col_idx = $mycur(first_col_idx);          \
  (v__)->cur_idx = this->cur_idx

#define state_restore(s__)                               \
  $mycur(first_col_idx) = (s__)->first_col_idx;          \
  $mycur(cur_col_idx) = (s__)->cur_col_idx;              \
  $my(video)->row_pos = (s__)->row_pos;                  \
  $my(video)->col_pos = (s__)->col_pos;                  \
  $my(video_first_row_idx) = (s__)->video_first_row_idx; \
  $my(video_first_row) = (s__)->video_first_row;         \
  $my(cur_video_row) = (s__)->row_pos;                   \
  $my(cur_video_col) = (s__)->col_pos

#define vundo_set(act, type__)                           \
  (act)->type = (type__);                                \
  state_set(act)

#define vundo_restore(act)                               \
  state_restore(act)

private void redo_clear (buf_t *this) {
  if ($my(redo)->head is NULL) return;

  action_t *action = $my(redo)->head;
  while (action isnot NULL) {
    action_t *tmp = action->next;
    buf_free_action (this, action);
    action = tmp;
  }
  $my(redo)->num_items = 0; $my(redo)->cur_idx = 0;
  $my(redo)->head = $my(redo)->tail = $my(redo)->current = NULL;
}

private void vundo_push (buf_t *this, action_t *action) {
  if ($my(undo)->num_items > $myroots(max_num_undo_entries)) {
    action_t *tmp = list_pop_tail ($my(undo), action_t);
    buf_free_action  (this, tmp);
  }

  ifnot ($my(undo)->state & VUNDO_RESET)
    redo_clear (this);
  else
    $my(undo)->state &= ~VUNDO_RESET;

  current_list_prepend ($my(undo), action);
}

private void redo_push (buf_t *this, action_t *action) {
  if ($my(redo)->num_items > $myroots(max_num_undo_entries)) {
    action_t *tmp = list_pop_tail ($my(redo), action_t);
    buf_free_action  (this, tmp);
  }

  current_list_prepend ($my(redo), action);
}

private action_t *vundo_pop (buf_t *this) {
  return current_list_pop ($my(undo), action_t);
}

private action_t *redo_pop (buf_t *this) {
  if ($my(redo)->head is NULL) return NULL;
  return current_list_pop ($my(redo), action_t);
}

private int vundo_insert (buf_t *this, act_t *act, action_t *redoact) {
  self(cur.set, act->idx);

  act_t *ract = AllocType (act);
  vundo_set (ract, DELETE_LINE);
  ract->idx = this->cur_idx;
  ract->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (redoact, ract);

  self(cur.delete);
  self(cur.set, act->cur_idx);

  buf_adjust_marks (this, DELETE_LINE, act->idx, act->idx);
  vundo_restore (act);

  return DONE;
}

private int vundo_delete_line (buf_t *this, act_t *act, action_t *redoact) {
  row_t *row = self(row.new_with, act->bytes);

  act_t *ract = AllocType (act);

  if (act->idx >= this->num_items) {
    self(cur.set, this->num_items - 1);
    vundo_set (ract, INSERT_LINE);
    self(cur.append, row);
    ract->idx = this->cur_idx;
  }
  else {
    self(cur.set, act->idx);
    vundo_set (ract, INSERT_LINE);
    self(cur.prepend, row);
    ract->idx = this->cur_idx;
  }

  stack_push (redoact, ract);
  buf_adjust_marks (this, INSERT_LINE, act->idx, act->idx + 1);

  vundo_restore (act);

  if ($my(video_first_row_idx) is this->cur_idx)
    $my(video_first_row) = this->current;

  return DONE;
}

private int vundo_replace_line (buf_t *this, act_t *act, action_t *redoact) {
  self(cur.set, act->idx);

  act_t *ract = AllocType (act);
  vundo_set (ract, REPLACE_LINE);
  ract->idx = this->cur_idx;
  ract->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (redoact, ract);

  My(String).replace_with ($mycur(data), act->bytes);

  vundo_restore (act);
  return DONE;
}

private int vundo (buf_t *this, utf8 com) {
  action_t *action = NULL;
  if (com is 'u')
    action = vundo_pop (this);
  else
    action = redo_pop (this);

  if (NULL is action) return NOTHING_TODO;

  act_t *act = stack_pop (action, act_t);

  action_t *redoact = AllocType (action);

  while (act isnot NULL) {
    if (act->type is DELETE_LINE)
      vundo_delete_line (this, act, redoact);
    else
      if (act->type is REPLACE_LINE)
        vundo_replace_line (this, act, redoact);
      else
        vundo_insert (this, act, redoact);

    free (act->bytes);
    free (act);
    act = stack_pop (action, act_t);
  }

  if (com is 'u')
    redo_push (this, redoact);
  else {
    $my(undo)->state |= VUNDO_RESET;
    vundo_push (this, redoact);
  }

  free (action);
  self(draw);
  return DONE;
}

private int ved_substitute (buf_t *this, char *pat, char *sub, int global,
int interactive, int fidx, int lidx) {
  int retval = NOTHING_TODO;
  row_t *it = this->head;

  string_t *substr = NULL;
  int flags = 0;
  regexp_t *re = My(Re).new (pat, flags, RE_MAX_NUM_CAPTURES, My(Re).compile);

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (action, act);

  int idx = 0;
  while (idx < fidx) {idx++; it = it->next;}
  while (idx++ <= lidx) {
    int bidx = 0;

searchandsub:;
    int done_substitution = 0;
    My(Re).reset_captures (re);

    if (0 > My(Re).exec (re, it->data->bytes + bidx,
        it->data->num_bytes - bidx)) goto thecontinue;

    ifnot (NULL is substr) string_free (substr);
    if (NULL is (substr = My(Re).parse_substitute (re, sub, re->match->bytes)))
      goto theend;

    if (interactive) {
      char prefix[bidx + re->match_idx + 1];
      memcpy (prefix, it->data->bytes, bidx + re->match_idx);
      prefix[bidx + re->match_idx] = '\0';
      utf8 chars[] = {'y', 'Y', 'n', 'N', 'q', 'Q', 'a', 'A', 'c', 'C'};
      utf8 c = quest (this, str_fmt (
          "|match at line %d byte idx %d|\n"
          "%s%s%s%s%s\n"
          "|substitution string|\n"
          "%s%s%s\n"
          "replace? yY[es]|nN[o] replace all?aA[ll], continue next line? cC[ontinue], quit? qQ[uit]\n",
           idx, re->match_idx, prefix, TERM_MAKE_COLOR(COLOR_MENU_SEL), re->match->bytes,
           TERM_MAKE_COLOR(COLOR_MENU_BG), re->match_ptr + re->match_len,
           TERM_MAKE_COLOR(COLOR_MENU_SEL), substr->bytes, TERM_MAKE_COLOR(COLOR_MENU_BG)),
           chars, ARRLEN (chars));

      switch (c) {
        case 'n': case 'N': goto if_global;
        case 'q': case 'Q': goto theend;
        case 'c': case 'C': goto thecontinue;
        case 'a': case 'A': interactive = 0;
      }
    }

    act = AllocType (act);
    vundo_set (act, REPLACE_LINE);
    act->idx = idx - 1;
    act->bytes = str_dup (it->data->bytes, it->data->num_bytes);
    stack_push (action, act);

    string_replace_numbytes_at_with (it->data, re->match_len, re->match_idx + bidx,
      substr->bytes);
    done_substitution = 1;

    retval = DONE;

if_global:
    if (global) {
      bidx += re->match_idx + (done_substitution ? re->match_len : 0) + 1;
      if (bidx >= (int) it->data->num_bytes) goto thecontinue;
      goto searchandsub;
    }

thecontinue:
    it = it->next;
  }

theend:
  if (retval is DONE) {
    $my(flags) |= BUF_IS_MODIFIED;
    vundo_push (this, action);
    self(draw);
  } else
    buf_free_action (this, action);

  My(Re).free (re);
  ifnot (NULL is substr) string_free (substr);

  return retval;
}

private int mark_set (buf_t *this, int mark) {
  if (mark < 0) {
    utf8 c = My(Input).get ($my(term_ptr));
    char *m = strchr (MARKS, c);
    if (NULL is m) return NOTHING_TODO;
    mark = m - MARKS;
  }

  state_set (&$my(marks)[mark]);
  $my(marks)[mark].cur_idx = this->cur_idx;

  if (mark isnot MARK_UNAMED)
    VED_MSG("set [%c] mark", MARKS[mark]);

  return DONE;
}

private int mark_goto (buf_t *this) {
  utf8 c = My(Input).get ($my(term_ptr));
  char *m = strchr (MARKS, c);
  if (NULL is m) return NOTHING_TODO;
  c = m - MARKS;
  mark_t *mark = &$my(marks)[c];
  if (mark->video_first_row is NULL) return NOTHING_TODO;
  if (mark->cur_idx is this->cur_idx) return NOTHING_TODO;
  if (mark->cur_idx >= this->num_items) return NOTHING_TODO;

  mark_t t;  state_set (&t);  t.cur_idx = this->cur_idx;

  self(cur.set, mark->cur_idx);
  state_restore (mark);

  if ($mycur(first_col_idx) or $mycur(cur_col_idx) >= (int) $mycur(data)->num_bytes) {
    $mycur(first_col_idx) = $mycur(cur_col_idx) = 0;
    $my(video)->col_pos = $my(cur_video_col) = 1;
  }

  $my(marks)[MARK_UNAMED] = t;

  self(draw);
  return DONE;
}

private utf8 quest (buf_t *this, char *qu, utf8 *chs, int len) {
  video_paint_rows_with ($my(video), -1, -1, -1, qu);
  SEND_ESC_SEQ ($my(video)->fd, TERM_CURSOR_HIDE);
  utf8 c;
  int found = 0;
  for (;;) {
    c = My(Input).get ($my(term_ptr));
    for (int i = 0; i < len; i++) {
      if ((found = chs[i] is c)) break;
    }
    if (found) break;
  }

  video_resume_painted_rows ($my(video));
  SEND_ESC_SEQ ($my(video)->fd, TERM_CURSOR_SHOW);
  return c;
}

private char *buf_parse_line (buf_t *this, row_t *row, char *line, int idx) {
  int numchars = 0;
  int j = 0;

  int maxn = row->data->num_bytes - row->first_col_idx;
  for (int i = 0; numchars < $my(dim)->num_cols && i < maxn; i++) {
      int len = char_byte_len (row->data->bytes[row->first_col_idx + i]);
      loop (len)
        line[j++] = row->data->bytes[row->first_col_idx + i++];

      i -= 1;
      numchars++;
    }

  line[j] = '\0';
  return $my(syn)->parse (this, line, j, idx, row);
}

private void buf_draw_cur_row (buf_t *this) {
  char line[MAXLINE];
  buf_parse_line (this, this->current, line, this->cur_idx);
  My(Video).set_with ($my(video), $my(video)->row_pos - 1, line);
  My(Video).Draw.row_at ($my(video), $my(video)->row_pos);
  buf_set_draw_statusline (this);
  My(Cursor).set_pos ($my(term_ptr), $my(video)->row_pos, $my(video)->col_pos);
}

private void buf_to_video (buf_t *this) {
  row_t *row = $my(video_first_row);
  int idx = $my(video_first_row_idx);
  char line[MAXLINE];

  int i;
  for (i = $my(dim)->first_row - 1; i < $my(statusline_row) - 1; i++) {
    if (row is NULL) break;
    buf_parse_line (this, row, line, idx++);
    My(Video).set_with ($my(video), i, line);
    row = row->next;
  }

  while (i < $my(statusline_row) - 1)
    My(Video).set_with ($my(video), i++, $my(ftype)->on_emptyline);

  buf_set_statusline (this);
}

private void buf_flush (buf_t *this) {
  video_render_set_from_to ($my(video), $my(dim)->first_row, $my(statusline_row));
  My(String).append_fmt ($my(video)->render, TERM_GOTO_PTR_POS_FMT,
      $my(video)->row_pos, $my(video)->col_pos);
  video_flush ($my(video), $my(video)->render);
}

private void buf_draw (buf_t *this) {
  My(String).clear ($my(video)->render);
  My(Ed).set.topline (this);
  video_render_set_from_to ($my(video), 1, 1);
  self(to.video);
  self(flush);
}

private int ved_quit (buf_t *this, int force) {
  if (force) return EXIT;

  buf_t *it = $my(parent)->head;

  while (it isnot NULL) {
    if (it->prop->flags & BUF_IS_MODIFIED) {
      utf8 chars[] = {'y', 'Y', 'n', 'N'};
      utf8 c = quest (this, str_fmt (
          "%s has been modified since last change\n"
          "continue writing? [yY|nN]", it->prop->fname), chars, ARRLEN (chars));
      switch (c) {case 'y': case 'Y': ved_write_buffer (it, 1);}
    }

    it = it->next;
  }

  return EXIT;
}

private void ved_on_blankline (buf_t *this) {
  int lineisblank = 1;
  for (int i = 0; i < (int) $mycur(data)->num_bytes; i++) {
    if ($mycur(data)->bytes[i] isnot ' ') {
      lineisblank = 0;
      break;
    }
  }

  if (lineisblank) {
    My(String).replace_with ($mycur(data), "");
    $my(video)->col_pos = $my(cur_video_col) = 0;
    $mycur(cur_col_idx) = $my(video)->col_pos = $my(cur_video_col) =
      $mycur(first_col_idx) = 0;
  }
}

private int ved_normal_right (buf_t *this, int count, int draw) {
  int is_ins_mode = str_eq ($my(mode), INSERT_MODE);

  if ($mycur(cur_col_idx) is ((int) $mycur(data)->num_bytes - 1 +
      is_ins_mode) or 0 is $mycur(data)->num_bytes or
      $mycur(data)->bytes[$mycur(cur_col_idx)] is 0)
    return NOTHING_TODO;

  int clen = char_byte_len ($mycur(data)->bytes[$mycur(cur_col_idx)]);
  char s[$mycur(data)->num_bytes - $mycur(cur_col_idx) + 1];
  int numbytes = ($mycur(data)->num_bytes - 1) - $mycur(cur_col_idx) - (clen - 1);
  memcpy (s, $mycur(data)->bytes + $mycur(cur_col_idx) + clen, numbytes);
  s[numbytes] = '\0';

  int len;
  int tlen = 0;
  int idx = 0;
  int curlen = char_byte_len ($mycur(data)->bytes[$mycur(cur_col_idx)]);

  while (numbytes and count--) {
    len = char_byte_len (s[idx++]);
    numbytes -= len;

    tlen += len;
    idx += len - 1;

    $mycur(cur_col_idx) += curlen;
    curlen = len;

    if ($my(video)->col_pos is $my(dim)->num_cols) {
      if (IS_MODE (INSERT_MODE)) {
        $mycur(first_col_idx) = $mycur(cur_col_idx);
        $my(video)->col_pos = $my(cur_video_col) = 1;
      } else
        $mycur(first_col_idx) += char_byte_len ($mycur(data)->bytes[$mycur(first_col_idx)]);
    }
    else {
      $my(video)->col_pos = $my(cur_video_col) = $my(video)->col_pos + 1;
    }
  }

  if (draw) self(draw_cur_row);
  return DONE;
}

private int ved_normal_noblnk (buf_t *this) {
  ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  $mycur(cur_col_idx) = 0;
  $my(video)->col_pos = $my(cur_video_col) = 1;

  for (int i = 0; i < (int) $mycur(data)->num_bytes; i++) {
    if (IS_SPACE ($mycur(data)->bytes[i])) {
      ved_normal_right (this, 1, 1);
      continue;
    }

    break;
  }

  return DONE;
}

private int ved_normal_eol (buf_t *this) {
  return ved_normal_right (this, $mycur(data)->num_bytes * 4, 1);
}

private int ved_normal_left (buf_t *this, int count) {
  if ($mycur(cur_col_idx) is 0) return NOTHING_TODO;

  int curcol = $mycur(first_col_idx);
  char s[$mycur(cur_col_idx) + 1];
  string_reverse_from_to (s, $mycur(data)->bytes, 0,  $mycur(cur_col_idx) - 1);

  int i = 0;
  while ($mycur(cur_col_idx) and count--) {
    int len = char_byte_len (s[i++]);
    $mycur(cur_col_idx) -= len;
    i += len - 1;

    if ($mycur(first_col_idx) and str_eq (NORMAL_MODE, $my(mode))) {
      char sa[$mycur(first_col_idx) + 1];
      string_reverse_from_to (sa, $mycur(data)->bytes, 0, $mycur(first_col_idx) - 1);
      $mycur(first_col_idx) -= char_byte_len (*sa);
    } else {
      if ($my(video)->col_pos is 1 and $mycur(first_col_idx)) {
        if ($mycur(first_col_idx) >= $my(dim)->num_cols) {
          $mycur(first_col_idx) -= $my(dim)->num_cols; /* fix */
          $my(video)->col_pos = $my(cur_video_col) = $my(dim)->num_cols;
        } else {
          $mycur(first_col_idx) = 0;
          $my(video)->col_pos = $my(cur_video_col) = str_eq (INSERT_MODE, $my(mode));
          for (int ii = 0; ii < $mycur(cur_col_idx); ii++) {
            int llen = char_byte_len ($mycur(data)->bytes[ii]);
            ii += llen - 1;
            $my(video)->col_pos = $my(cur_video_col) = $my(video)->col_pos + 1;
          }
        }
      } else {
        $my(video)->col_pos -= $my(video)->col_pos > 1;
        $my(cur_video_col) = $my(video)->col_pos;
      }
    }
  }

  if ($mycur(first_col_idx) isnot curcol) {
    self(draw_cur_row);
  } else {
    buf_set_draw_statusline (this);
    My(Cursor).set_pos ($my(term_ptr), $my(video)->row_pos, $my(video)->col_pos);
  }

  return DONE;
}

private int ved_normal_bol (buf_t *this) {
  return ved_normal_left (this, $mycur(data)->num_bytes * 4);
}

private int ved_normal_end_word (buf_t *this, int count, int run_insert_mode) {
  ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  int cur_idx = $mycur(cur_col_idx);
  int retval = NOTHING_TODO;
  for (int i = 0; i < count; i++) {
    while (($mycur(cur_col_idx) isnot (int) $mycur(data)->num_bytes - 1) and
          (0 is (IS_SPACE ($mycur(data)->bytes[$mycur(cur_col_idx)]))))
      retval = ved_normal_right (this, 1, 1);

    if (NOTHING_TODO is retval) break;
    if (NOTHING_TODO is (retval = ved_normal_right (this, 1, 1))) break;
  }

  if (cur_idx is $mycur(cur_col_idx)) return NOTHING_TODO;
  if (retval is DONE) ved_normal_left (this, 1);
  if (run_insert_mode) return ved_insert (this, 'i');
  if (IS_SPACE ($mycur(data)->bytes[$mycur(cur_col_idx)]))
    ved_normal_left (this, 1);

  return DONE;
}

private int ved_normal_up (buf_t *this, int count, int adjust_col, int draw) {
  int currow_idx = this->cur_idx;

  if (0 is currow_idx or 0 is count) return NOTHING_TODO;
  if (count > currow_idx) count = currow_idx;

  int curcol_idx = $mycur(cur_col_idx);
  int clen = char_byte_len ($mycur(data)->bytes[curcol_idx]);
  int isatend = (curcol_idx + clen) is (int) $mycur(data)->num_bytes;

  ved_on_blankline (this);

  currow_idx -= count;
  self(cur.set, currow_idx);

  int col_pos = adjust_col ? buf_adjust_col (this, curcol_idx, isatend) : 1;
  int row = $my(video)->row_pos;
  int orig_count = count;

  if (row > count)
    while (count && row - count >= $my(dim)->first_row)
      count--;

  ifnot (adjust_col) $mycur(cur_col_idx) = 0;

  if (count) {
    if (count <= $my(video_first_row_idx)) {
      self(set.video_first_row, $my(video_first_row_idx) - count);
      $my(video)->col_pos = $my(cur_video_col) = col_pos;
    } else {
      self(set.video_first_row, currow_idx);
      $my(video)->row_pos = $my(cur_video_row) = $my(dim)->first_row;
      $my(video)->col_pos = $my(cur_video_col) = col_pos;
    }
  } else {
    $my(video)->row_pos = $my(cur_video_row) = row - orig_count;
    $my(video)->col_pos = $my(cur_video_col) = col_pos;
    if (draw) self(draw_cur_row);
    return DONE;
  }

  if (draw) self(draw);
  return DONE;
}

private int ved_normal_down (buf_t *this, int count, int adjust_col, int draw) {
  int currow_idx = this->cur_idx;
  if (this->num_items - 1 is currow_idx)
    return NOTHING_TODO;

  if (count + currow_idx >= this->num_items)
    count = this->num_items - currow_idx - 1;

  int curcol_idx = $mycur(cur_col_idx);
  int clen = char_byte_len ($mycur(data)->bytes[curcol_idx]);
  int isatend = (curcol_idx + clen) is (int) $mycur(data)->num_bytes;

  ved_on_blankline (this);

  currow_idx += count;
  self(cur.set, currow_idx);

  int col_pos = adjust_col ? buf_adjust_col (this, curcol_idx, isatend) : 1;

  int row = $my(video)->row_pos;
  int orig_count = count;

  while (count && count + row < $my(statusline_row))
    count--;

  ifnot (adjust_col) $mycur(cur_col_idx) = 0;

  if (count) {
    self(set.video_first_row, $my(video_first_row_idx) + count);
    $my(video)->col_pos = $my(cur_video_col) = col_pos;
  } else {
    $my(video)->row_pos = $my(cur_video_row) = row + orig_count;
    $my(video)->col_pos = $my(cur_video_col) = col_pos;
    if (draw) self(draw_cur_row);
    return DONE;
  }

  if (draw) self(draw);
  return DONE;
}

private int ved_normal_page_down (buf_t *this, int count) {
  if (this->num_items < ONE_PAGE
      or this->num_items - $my(video_first_row_idx) < ONE_PAGE + 1)
    return NOTHING_TODO;

  mark_set (this, MARK_UNAMED);

  int curcol_idx = $mycur(cur_col_idx);
  int clen = char_byte_len ($mycur(data)->bytes[curcol_idx]);
  int isatend = (curcol_idx + clen) is (int) $mycur(data)->num_bytes;

  int row = $my(video)->row_pos;

  int frow = $my(video_first_row_idx);
  int currow_idx = this->cur_idx;

  while (count--) {
    frow += ONE_PAGE; currow_idx += ONE_PAGE;
    if (frow >= this->num_items or currow_idx >= this->num_items) {
      frow = this->num_items - ONE_PAGE;
      row = $my(dim->first_row) + 1;
      currow_idx = frow + 1;
      break;
      }
  }

  ved_on_blankline (this);

  self(cur.set, currow_idx);
  self(set.video_first_row, frow);
  $my(video)->row_pos = $my(cur_video_row) = row;
  $my(video)->col_pos = $my(cur_video_col) = buf_adjust_col (this, curcol_idx, isatend);
  self(draw);
  return DONE;
}

private int ved_normal_page_up (buf_t *this, int count) {
  if ($my(video_first_row_idx) is 0 or this->num_items < ONE_PAGE)
    return NOTHING_TODO;

  mark_set (this, MARK_UNAMED);

  int curcol_idx = $mycur(cur_col_idx);
  int clen = char_byte_len ($mycur(data)->bytes[curcol_idx]);
  int isatend = (curcol_idx + clen) is (int) $mycur(data)->num_bytes;

  int row = $my(video)->row_pos;
  int frow = $my(video_first_row_idx);
  int curlnr = this->cur_idx;

  while (count--) {
    frow -= ONE_PAGE;
    if (frow <= 0) {
      curlnr = 0 > frow ? 0 : curlnr - ONE_PAGE;
      row = frow < 0 ? $my(dim)->first_row : row;
      frow = 0;
      break;
      }

    curlnr -= ONE_PAGE;
  }

  ved_on_blankline (this);

  self(cur.set, curlnr);
  self(set.video_first_row, frow);
  $my(video)->row_pos = $my(cur_video_row) = row;
  $my(video)->col_pos = $my(cur_video_col) = buf_adjust_col (this, curcol_idx, isatend);

  self(draw);
  return DONE;
}

private int ved_normal_bof (buf_t *this) {
  if (this->cur_idx is 0) return NOTHING_TODO;

  mark_set (this, MARK_UNAMED);

  int curcol_idx = $mycur(cur_col_idx);
  int clen = char_byte_len ($mycur(data)->bytes[curcol_idx]);
  int isatend = (curcol_idx + clen) is (int) $mycur(data)->num_bytes;

  ved_on_blankline (this);

  self(set.video_first_row, 0);
  self(cur.set, 0);

  $my(video)->row_pos = $my(cur_video_row) = $my(dim)->first_row;
  $my(video)->col_pos = $my(cur_video_col) = buf_adjust_col (this, curcol_idx, isatend);

  self(draw);
  return DONE;
}

private int ved_normal_eof (buf_t *this) {
  if ($my(video_first_row_idx) is this->num_items - 1)
    return NOTHING_TODO;

  mark_set (this, MARK_UNAMED);

  int curcol_idx = $mycur(cur_col_idx);
  int clen = char_byte_len ($mycur(data)->bytes[curcol_idx]);
  int isatend = (curcol_idx + clen) is (int) $mycur(data)->num_bytes;

  ved_on_blankline (this);

  self(cur.set, this->num_items - 1);

  $my(video)->col_pos = $my(cur_video_col) = buf_adjust_col (this, curcol_idx, isatend);

  if (this->num_items < ONE_PAGE) {
    $my(video)->row_pos = $my(cur_video_row) = $my(dim)->first_row + this->num_items;
    buf_set_draw_statusline (this);
    My(Cursor).set_pos ($my(term_ptr), $my(video)->row_pos, $my(video)->col_pos);
    return DONE;
  } else {
    self (set.video_first_row, this->num_items - (ONE_PAGE));
    $my(video)->row_pos = $my(cur_video_row) = $my(statusline_row) - 1;
  }

  self(draw);
  return DONE;
}

private int ved_normal_goto_linenr (buf_t *this, int lnr) {
  int currow_idx = this->cur_idx;

  if (lnr <= 0 or lnr is currow_idx + 1 or lnr > this->num_items)
    return NOTHING_TODO;

  mark_set (this, MARK_UNAMED);

  if (lnr < currow_idx + 1)
    return ved_normal_up (this, currow_idx - lnr + 1, 1 , 1);

  return ved_normal_down (this, lnr - currow_idx - 1, 1, 1);
}

private int ved_normal_replace_char (buf_t *this) {
  if ($mycur(data)->num_bytes is 0) return NOTHING_TODO;

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (action, act);
  vundo_push (this, action);

  utf8 c = My(Input).get ($my(term_ptr));
  char buf[5];
  char_from_code (c, buf);
  int clen = char_byte_len ($mycur(data)->bytes[$mycur(cur_col_idx)]);
  My(String).replace_numbytes_at_with ($mycur(data), clen,
    $mycur(cur_col_idx), buf);

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);
  return DONE;
}

private int ved_normal_delete_eol (buf_t *this, int regidx) {
  if ($mycur(data)->num_bytes is 0 or
      $mycur(cur_col_idx) is (int) $mycur(data)->num_bytes)
    return NOTHING_TODO;

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (action, act);
  vundo_push (this, action);

  rg_t *rg = NULL;
  rg = &$my(regs)[regidx];
  My(Ed).free_reg ($my(root), rg);
  rg->reg = regidx;
  rg->type = CHARWISE;

  int clen = char_byte_len ($mycur(data)->bytes[$mycur(cur_col_idx)]);

  int len = $mycur(data)->num_bytes - $mycur(cur_col_idx);

  char buf[len + 1];
  memcpy (buf, $mycur(data)->bytes + $mycur(cur_col_idx), len);

  buf[len] = '\0';
  reg_t *reg = AllocType (reg);
  reg->data = My(String).new_with (buf);
  stack_push (rg, reg);

  My(String).delete_numbytes_at ($mycur(data),
     $mycur(data)->num_bytes - $mycur(cur_col_idx), $mycur(cur_col_idx));

  $mycur(cur_col_idx) -= clen;
  if ($mycur(cur_col_idx) is 0)
    $my(video)->col_pos = $my(cur_video_col) = 1;
  else
    if (0 is $mycur(first_col_idx))
      $my(video)->col_pos = $my(cur_video_col) = $my(video)->col_pos - 1;
    else {
      if (0 is $mycur(first_col_idx) - clen)
        $mycur(first_col_idx) -= clen;
      else
        $my(video)->col_pos = $my(cur_video_col) = $my(video)->col_pos - 1;
    }

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);
  return DONE;
}

private int ved_insert_new_line (buf_t *this,  utf8 com) {
  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, INSERT_LINE);

  int new_idx = this->cur_idx + ('o' is com ? 1 : -1);
  int currow_idx = this->cur_idx;

  if ('o' is com) {
    self(cur.append_with, " ");
    My(String).clear ($mycur(data));
    act->idx = this->cur_idx;
    this->current = this->current->prev;
    this->cur_idx--;
    ved_normal_down (this, 1, 0, 1);
  } else {
    self(cur.prepend_with, " ");
    My(String).clear ($mycur(data));
    act->idx = this->cur_idx;

    if ($my(video_first_row_idx) is this->cur_idx and 0 is this->cur_idx) {
      $my(video_first_row_idx)--;
      $my(video_first_row) = $my(video_first_row)->prev;
    }

    this->current = this->current->next;
    this->cur_idx++;
    if ($my(video)->row_pos isnot $my(dim)->first_row)
      $my(video)->row_pos = $my(cur_video_row) = $my(video)->row_pos + 1;

    ved_normal_up (this, 1, 1, 1);
  }

  stack_push (action, act);
  vundo_push (this, action);

  if (currow_idx > new_idx) {int t = new_idx; new_idx = currow_idx; currow_idx = t;}
  buf_adjust_marks (this, INSERT_LINE, currow_idx, new_idx);

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw);
  return ved_insert (this, com);
}

private int ved_join (buf_t *this) {
  if (this->num_items is 0 or this->num_items - 1 is this->cur_idx)
    return NOTHING_TODO;

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (action, act);

  row_t *row = buf_current_pop_next (this);
  act_t *act2 = AllocType (act);
  vundo_set (act2, DELETE_LINE);
  act2->idx = this->cur_idx + 1;
  act2->bytes = str_dup (row->data->bytes, row->data->num_bytes);

  stack_push (action, act2);
  vundo_push (this, action);

  if (row->data->num_bytes isnot 0) {
    RM_TRAILING_NEW_LINE;
    My(String).append ($mycur(data), row->data->bytes);
  }

  buf_adjust_marks (this, DELETE_LINE, this->cur_idx, this->cur_idx + 1);
  $my(flags) |= BUF_IS_MODIFIED;
  self(free.row, row);
  self(draw);
  return DONE;
}

private int ved_normal_delete (buf_t *this, int count, int regidx) {
  ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  action_t *action = NULL;  act_t *act = NULL;
  rg_t *rg = NULL;

  int is_norm_mode = IS_MODE (NORMAL_MODE);
  int perfom_undo = is_norm_mode or IS_MODE (VISUAL_MODE_CW) or IS_MODE (VISUAL_MODE_BW);

  if (perfom_undo) {
    action = AllocType (action);
    act = AllocType (act);
    vundo_set (act, REPLACE_LINE);
    act->idx = this->cur_idx;
    act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
    rg = &$my(regs)[regidx];
    My(Ed).free_reg ($my(root), rg);
    rg->reg = regidx;
    rg->type = CHARWISE;
  }

  int len = 0;
  int clen = 1;

  while (count--) {
    if ($mycur(cur_col_idx) + len is (int) $mycur(data)->num_bytes)
      break;

    clen = char_byte_len ($mycur(data)->bytes[$mycur(cur_col_idx) + len++]);
    len += clen - 1;
  }

  char buf[len + 1];
  memcpy (buf, $mycur(data)->bytes, len);  buf[len] = '\0';
  My(String).delete_numbytes_at ($mycur(data), len, $mycur(cur_col_idx));

  if ($mycur(cur_col_idx) is (int) $mycur(data)->num_bytes - clen + (1 + (0 is is_norm_mode)))
    ved_normal_left (this, 1);

  if (perfom_undo) {
    act->cur_col_idx = $mycur(cur_col_idx);
    act->first_col_idx = $mycur(first_col_idx);

    stack_push (action, act);
    vundo_push (this, action);
    reg_t *reg = AllocType (reg);
    reg->data = My(String).new_with (buf);
    stack_push (rg, reg);
  }

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);
  return DONE;
}

private int ved_inc_dec_char (buf_t *this, utf8 com) {
  utf8 c = cur_utf8_code;
  if (' ' > c) return NOTHING_TODO;
  if (CTRL('a') is com) {
    c++;
  } else {
    c--;
  }

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->num_bytes = $mycur(data)->num_bytes;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  action = stack_push (action, act);
  vundo_push (this, action);

  char ch[5]; char_from_code (c, ch);
  My(String).replace_numbytes_at_with ($mycur(data), 1, $mycur(cur_col_idx), ch);
  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);
  return DONE;
}

private int ved_word_math (buf_t *this, int count, utf8 com) {
  int fidx;

  string_t *word = get_current_number (this, &fidx);
  if (NULL is word) return ved_inc_dec_char (this, com);

  char *p = word->bytes; int type = *p++;
  int nr = atoi (p);
  if (type isnot 'd') goto theend;
  if (CTRL('a') is com) {
    nr += count;
  } else {
    nr -= count;
  }

  char new[128];
  itoa (nr, new, 10);

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->num_bytes = $mycur(data)->num_bytes;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  action = stack_push (action, act);
  vundo_push (this, action);

  My(String).replace_numbytes_at_with ($mycur(data), word->num_bytes - 1, fidx, new);
  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);

theend:
  string_free (word);
  return DONE;
}

private int ved_complete_word_callback (menu_t *menu) {
  buf_t *this = menu->this;

  int idx = 0;
  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;

    idx = $mycur(cur_col_idx) - ($mycur(cur_col_idx) isnot 0);

    while (idx >= 0 and (
         IS_SPACE ($mycur(data)->bytes[idx]) is 0 and
         IS_CNTRL ($mycur(data)->bytes[idx]) is 0 and
         NULL is memchr (Notword, $mycur(data)->bytes[idx], Notword_len)))
      idx--;

    $my(shared_int) = idx + 1;

    idx++;

    int clen = char_byte_len ($mycur(data)->bytes[$mycur(cur_col_idx)]);

    menu->pat[0] = '\0';
    menu->patlen = 0;

    while (idx < $mycur(cur_col_idx)) {
      menu->pat[menu->patlen++] = $mycur(data)->bytes[idx];
      idx++;
    }

    if ($mycur(data)->bytes[idx] and IS_SPACE ($mycur(data)->bytes[idx]) is 0)
      for (int i = 0; i < clen; i++)
        menu->pat[menu->patlen++] =$mycur(data)->bytes[idx++];

    menu->pat[menu->patlen] = '\0';
  } else
    menu_free_list (menu);

  vstr_t *words = AllocType (vstr);

  row_t *row = this->head;
  idx = -1;

  while (row isnot NULL) {
    if (row->data->bytes and ++idx isnot this->cur_idx) {
      char *p = strstr (row->data->bytes, menu->pat);
      if (NULL isnot p) {
        char word[MAXWORDLEN];
        int lidx = 0;
        while (*p and (
            IS_SPACE (*p) is 0 and
            IS_CNTRL (*p) is 0 and
            NULL is memchr (Notword, *p, Notword_len))) {
          word[lidx++] = *p++;
        }

        word[lidx] = '\0';
        vstr_add_sort_and_uniq (words, word);
      }
    }
    row = row->next;
  }

  ifnot (words->num_items) {
    free (words);
    menu->state |= MENU_QUIT;
    return NOTHING_TODO;
  }

  menu->list = words;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  return DONE;
}

private int ved_complete_word (buf_t *this) {
  int retval = DONE;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2, $my(video)->col_pos,
  ved_complete_word_callback, NULL, 0);
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;
  menu->space_selects = 0;

  int orig_patlen = menu->patlen;

  char *word = menu_create ($my(root), menu);
  if (word isnot NULL) {
    action_t *action = AllocType (action);
    act_t *act = AllocType (act);
    vundo_set (act, REPLACE_LINE);
    act->idx = this->cur_idx;
    act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
    stack_push (action, act);
    vundo_push (this, action);
    My(String).delete_numbytes_at ($mycur(data), orig_patlen, $my(shared_int));
    My(String).insert_at ($mycur(data), word, $my(shared_int));
    ved_normal_end_word (this, 1, 0);
    ved_normal_right (this, 1, 1);
    $my(flags) |= BUF_IS_MODIFIED;
    self(draw_cur_row);
  }

theend:
  menu_free (menu);
  return retval;
}

private int ved_complete_line_callback (menu_t *menu) {
  buf_t *this = menu->this;

  int idx = 0;

  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
    menu->pat[0] = '\0';
    menu->patlen = 0;

    char *s = $mycur(data)->bytes;

    while (IS_SPACE (*s)) { s++; idx++; }
    int clen = char_byte_len ($mycur(data)->bytes[$mycur(cur_col_idx)]);

    while (idx++ < $mycur(cur_col_idx) + (clen - 1))
      menu->pat[menu->patlen++] = *s++;

    menu->pat[menu->patlen] = '\0';
  } else
    menu_free_list (menu);

  vstr_t *lines = AllocType (vstr);

  row_t *row = this->head;
  idx = 0;

  while (row isnot NULL) {
    if (row->data->bytes and idx++ isnot this->cur_idx) {
      char *p = strstr (row->data->bytes, menu->pat);
      if (NULL isnot p) {
        if (p is row->data->bytes or ({
            int i = p - row->data->bytes;
            int found = 1;
            while (i--) {
              if (IS_SPACE (row->data->bytes[i]) is 0) {
                found = 0; break;
              }
            }
            found;}))
        vstr_add_sort_and_uniq (lines, row->data->bytes);
       }
    }
    row = row->next;
  }

  ifnot (lines->num_items) {
    free (lines);
    menu->state |= MENU_QUIT;
    return NOTHING_TODO;
  }

  menu->list = lines;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  return DONE;
}

private int ved_complete_line (buf_t *this) {
  int retval = DONE;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2, $my(video)->col_pos,
ved_complete_line_callback, NULL, 0);
  menu->space_selects = 0;
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;

  char *line = menu_create ($my(root), menu);

  if (line isnot NULL) {
    action_t *action = AllocType (action);
    act_t *act = AllocType (act);
    vundo_set (act, REPLACE_LINE);
    act->idx = this->cur_idx;
    act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
    stack_push (action, act);
    vundo_push (this, action);
    My(String).clear ($mycur(data));
    My(String).append ($mycur(data), line);
    $my(flags) |= BUF_IS_MODIFIED;
    self(draw_cur_row);
  }

theend:
  menu_free (menu);
  return retval;
}

private int ved_win_only (buf_t *this) {
  if (1 is $myparents(num_frames)) return NOTHING_TODO;

  for (int i = $myparents(num_frames) - 1; i > 0; i--)
    win_delete_frame ($my(parent), i);

  buf_t *it = $my(parent)->head;
  int idx = 0;
  while (it isnot NULL) {
    if (idx++ isnot $my(parent)->cur_idx)
      it->prop->is_visible = 0;
    else {
      $my(video)->row_pos = $my(cur_video_row);
      $my(video)->col_pos = $my(cur_video_col);
    }

    it = it->next;
  }

  My(Win).draw ($my(parent));
  return DONE;
}

private int ved_normal_handle_ctrl_w (buf_t **thisp) {
  buf_t *this = *thisp;

  utf8 c = My(Input).get ($my(term_ptr));
  switch (c) {
    case CTRL('w'):
    case ARROW_DOWN_KEY:
    case 'j':
    case 'w':
       {
         int frame = $myparents(cur_frame) + 1;
         if (frame is $myparents(num_frames)) frame = 0;
         this = win_change_frame ($my(parent), frame);
         if (NULL isnot this) {
           *thisp = this;
           return DONE;
         }
       }
       break;

    case ARROW_UP_KEY:
    case 'k':
       {
         int frame = $myparents(cur_frame) - 1;
         if (frame is -1) frame = $myparents(num_frames) - 1;
         this = win_change_frame ($my(parent), frame);
         if (NULL isnot this) {
           *thisp = this;
           return DONE;
         }
       }
       break;

    case 'o':
      return ved_win_only (this);

    case 'n':
      return ved_enew_fname (thisp, UNAMED);

    case 's':
      return ved_split (thisp, UNAMED);

    case 'l':
    case ARROW_RIGHT_KEY:
      return ved_win_change (thisp, VED_COM_WIN_CHANGE_NEXT, NULL, 0);

    case 'h':
    case ARROW_LEFT_KEY:
      return ved_win_change (thisp, VED_COM_WIN_CHANGE_PREV, NULL, 0);

    case '`':
      return ved_win_change (thisp, VED_COM_WIN_CHANGE_PREV_FOCUSED, NULL, 0);

    default:
      break;
  }

  return NOTHING_TODO;
}


private int ved_normal_handle_g (buf_t *this, int count) {
  if (1 isnot count)
    return ved_normal_goto_linenr (this, count);

  utf8 c = My(Input).get ($my(term_ptr));
  switch (c) {
    case 'g':
      return ved_normal_bof (this);

    default:
      return NOTHING_TODO;
  }

  return ved_normal_goto_linenr (this, count);
}

private int ved_normal_handle_G (buf_t *this, int count) {
  if (1 isnot count)
    return ved_normal_goto_linenr (this, count);

  return ved_normal_eof (this);
}

private int ved_handle_ctrl_x (buf_t *this) {
  utf8 c = My(Input).get ($my(term_ptr));
  switch (c) {
    case 'l':
    case CTRL('l'):
      return ved_complete_line (this);
  }

  return NOTHING_TODO;
}

private int ved_delete_line (buf_t *this, int count, int regidx) {
  if (count > this->num_items - this->cur_idx)
    count = this->num_items - this->cur_idx;

  rg_t *rg = &$my(regs)[regidx];
  My(Ed).free_reg ($my(root), rg);
  rg->reg = regidx;
  rg->type = LINEWISE;

  int currow_idx = this->cur_idx;
  int curcol_idx = $mycur(cur_col_idx);
  int clen = char_byte_len ($mycur(data)->bytes[curcol_idx]);
  int isatend = (curcol_idx + clen) is (int) $mycur(data)->num_bytes;

  action_t *action = AllocType (action);

  int fidx = this->cur_idx;
  int lidx = fidx + count - 1;

  reg_t *reg = NULL;

  for (int idx = fidx; idx <= lidx; idx++) {
    act_t *act = AllocType (act);
    vundo_set (act, DELETE_LINE);
    act->idx = this->cur_idx;
    act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
    stack_push (action, act);

    reg_t *r = AllocType (reg);
    r->data = My(String).new_with ($mycur(data)->bytes);
    rg->cur_col_idx = $mycur(cur_col_idx);
    rg->first_col_idx = $mycur(first_col_idx);
    rg->col_pos = $my(cur_video_col);
    if (reg is NULL) { r->prev = NULL; reg = r; rg->head = reg; }
    else { r->prev = reg; reg->next = r; reg = reg->next; }

    if (NULL is self(cur.delete)) break;
  }

  if (this->num_items is 0) buf_on_no_length (this);

  buf_adjust_col (this, curcol_idx, isatend);
  buf_adjust_marks (this, DELETE_LINE, fidx, lidx);

  if (this->cur_idx is currow_idx) {
    if ($my(video_first_row_idx) < fidx) {
       goto theend;
    } else {
      $my(video_first_row) = this->current;
      $my(video_first_row_idx) = this->cur_idx;
      goto theend;
    }
  }

  if ($my(video_first_row_idx) > this->cur_idx or
      $my(video_first_row_idx) < this->cur_idx)
    buf_adjust_view (this);

theend:
  $my(flags) |= BUF_IS_MODIFIED;
  VED_MSG("deleted into register [%c]", REGISTERS[regidx]);
  self(draw);
  vundo_push (this, action);
  return DONE;
}

/* code from the utf8.h project: https://github.com/sheredom/utf8.h.git
 */

private utf8 lowercodepoint (utf8 cp) {
  if (((0x0041 <= cp) && (0x005a >= cp)) ||
      ((0x00c0 <= cp) && (0x00d6 >= cp)) ||
      ((0x00d8 <= cp) && (0x00de >= cp)) ||
      ((0x0391 <= cp) && (0x03a1 >= cp)) ||
      ((0x03a3 <= cp) && (0x03ab >= cp))) {
    cp += 32;
  } else if (((0x0100 <= cp) && (0x012f >= cp)) ||
             ((0x0132 <= cp) && (0x0137 >= cp)) ||
             ((0x014a <= cp) && (0x0177 >= cp)) ||
             ((0x0182 <= cp) && (0x0185 >= cp)) ||
             ((0x01a0 <= cp) && (0x01a5 >= cp)) ||
             ((0x01de <= cp) && (0x01ef >= cp)) ||
             ((0x01f8 <= cp) && (0x021f >= cp)) ||
             ((0x0222 <= cp) && (0x0233 >= cp)) ||
             ((0x0246 <= cp) && (0x024f >= cp)) ||
             ((0x03d8 <= cp) && (0x03ef >= cp))) {
    cp |= 0x1;
  } else if (((0x0139 <= cp) && (0x0148 >= cp)) ||
             ((0x0179 <= cp) && (0x017e >= cp)) ||
             ((0x01af <= cp) && (0x01b0 >= cp)) ||
             ((0x01b3 <= cp) && (0x01b6 >= cp)) ||
             ((0x01cd <= cp) && (0x01dc >= cp))) {
    cp += 1;
    cp &= ~0x1;
  } else {
    switch (cp) {
    default: break;
    case 0x0178: cp = 0x00ff; break;
    case 0x0243: cp = 0x0180; break;
    case 0x018e: cp = 0x01dd; break;
    case 0x023d: cp = 0x019a; break;
    case 0x0220: cp = 0x019e; break;
    case 0x01b7: cp = 0x0292; break;
    case 0x01c4: cp = 0x01c6; break;
    case 0x01c7: cp = 0x01c9; break;
    case 0x01ca: cp = 0x01cc; break;
    case 0x01f1: cp = 0x01f3; break;
    case 0x01f7: cp = 0x01bf; break;
    case 0x0187: cp = 0x0188; break;
    case 0x018b: cp = 0x018c; break;
    case 0x0191: cp = 0x0192; break;
    case 0x0198: cp = 0x0199; break;
    case 0x01a7: cp = 0x01a8; break;
    case 0x01ac: cp = 0x01ad; break;
    case 0x01af: cp = 0x01b0; break;
    case 0x01b8: cp = 0x01b9; break;
    case 0x01bc: cp = 0x01bd; break;
    case 0x01f4: cp = 0x01f5; break;
    case 0x023b: cp = 0x023c; break;
    case 0x0241: cp = 0x0242; break;
    case 0x03fd: cp = 0x037b; break;
    case 0x03fe: cp = 0x037c; break;
    case 0x03ff: cp = 0x037d; break;
    case 0x037f: cp = 0x03f3; break;
    case 0x0386: cp = 0x03ac; break;
    case 0x0388: cp = 0x03ad; break;
    case 0x0389: cp = 0x03ae; break;
    case 0x038a: cp = 0x03af; break;
    case 0x038c: cp = 0x03cc; break;
    case 0x038e: cp = 0x03cd; break;
    case 0x038f: cp = 0x03ce; break;
    case 0x0370: cp = 0x0371; break;
    case 0x0372: cp = 0x0373; break;
    case 0x0376: cp = 0x0377; break;
    case 0x03f4: cp = 0x03d1; break;
    case 0x03cf: cp = 0x03d7; break;
    case 0x03f9: cp = 0x03f2; break;
    case 0x03f7: cp = 0x03f8; break;
    case 0x03fa: cp = 0x03fb; break;
    };
  }

  return cp;
}

private utf8 uppercodepoint(utf8 cp) {
  if (((0x0061 <= cp) && (0x007a >= cp)) ||
      ((0x00e0 <= cp) && (0x00f6 >= cp)) ||
      ((0x00f8 <= cp) && (0x00fe >= cp)) ||
      ((0x03b1 <= cp) && (0x03c1 >= cp)) ||
      ((0x03c3 <= cp) && (0x03cb >= cp))) {
    cp -= 32;
  } else if (((0x0100 <= cp) && (0x012f >= cp)) ||
             ((0x0132 <= cp) && (0x0137 >= cp)) ||
             ((0x014a <= cp) && (0x0177 >= cp)) ||
             ((0x0182 <= cp) && (0x0185 >= cp)) ||
             ((0x01a0 <= cp) && (0x01a5 >= cp)) ||
             ((0x01de <= cp) && (0x01ef >= cp)) ||
             ((0x01f8 <= cp) && (0x021f >= cp)) ||
             ((0x0222 <= cp) && (0x0233 >= cp)) ||
             ((0x0246 <= cp) && (0x024f >= cp)) ||
             ((0x03d8 <= cp) && (0x03ef >= cp))) {
    cp &= ~0x1;
  } else if (((0x0139 <= cp) && (0x0148 >= cp)) ||
             ((0x0179 <= cp) && (0x017e >= cp)) ||
             ((0x01af <= cp) && (0x01b0 >= cp)) ||
             ((0x01b3 <= cp) && (0x01b6 >= cp)) ||
             ((0x01cd <= cp) && (0x01dc >= cp))) {
    cp -= 1;
    cp |= 0x1;
  } else {
    switch (cp) {
    default: break;
    case 0x00ff: cp = 0x0178; break;
    case 0x0180: cp = 0x0243; break;
    case 0x01dd: cp = 0x018e; break;
    case 0x019a: cp = 0x023d; break;
    case 0x019e: cp = 0x0220; break;
    case 0x0292: cp = 0x01b7; break;
    case 0x01c6: cp = 0x01c4; break;
    case 0x01c9: cp = 0x01c7; break;
    case 0x01cc: cp = 0x01ca; break;
    case 0x01f3: cp = 0x01f1; break;
    case 0x01bf: cp = 0x01f7; break;
    case 0x0188: cp = 0x0187; break;
    case 0x018c: cp = 0x018b; break;
    case 0x0192: cp = 0x0191; break;
    case 0x0199: cp = 0x0198; break;
    case 0x01a8: cp = 0x01a7; break;
    case 0x01ad: cp = 0x01ac; break;
    case 0x01b0: cp = 0x01af; break;
    case 0x01b9: cp = 0x01b8; break;
    case 0x01bd: cp = 0x01bc; break;
    case 0x01f5: cp = 0x01f4; break;
    case 0x023c: cp = 0x023b; break;
    case 0x0242: cp = 0x0241; break;
    case 0x037b: cp = 0x03fd; break;
    case 0x037c: cp = 0x03fe; break;
    case 0x037d: cp = 0x03ff; break;
    case 0x03f3: cp = 0x037f; break;
    case 0x03ac: cp = 0x0386; break;
    case 0x03ad: cp = 0x0388; break;
    case 0x03ae: cp = 0x0389; break;
    case 0x03af: cp = 0x038a; break;
    case 0x03cc: cp = 0x038c; break;
    case 0x03cd: cp = 0x038e; break;
    case 0x03ce: cp = 0x038f; break;
    case 0x0371: cp = 0x0370; break;
    case 0x0373: cp = 0x0372; break;
    case 0x0377: cp = 0x0376; break;
    case 0x03d1: cp = 0x03f4; break;
    case 0x03d7: cp = 0x03cf; break;
    case 0x03f2: cp = 0x03f9; break;
    case 0x03f8: cp = 0x03f7; break;
    case 0x03fb: cp = 0x03fa; break;
    };
  }

  return cp;
}

private int utf8islower (utf8 chr) {
  return chr != uppercodepoint (chr);
}

private int utf8isupper (utf8 chr) {
  return chr != lowercodepoint (chr);
}

private int ved_normal_change_case (buf_t *this) {
  utf8 c = cur_utf8_code;
  char buf[5];
  action_t *action;
  act_t *act;

  if ('a' <= c and c <= 'z')
    buf[0] = c - ('a' - 'A');
  else if ('A' <= c and c <= 'Z')
    buf[0] = c + ('a' - 'A');
  else {
    char *p = strchr (CASE_A, $mycur(data)->bytes[$mycur(cur_col_idx)]);
    if (NULL is p) {
      if (c > 0x80) {
        utf8 new;
        if (utf8islower (c))
          new = uppercodepoint (c);
        else
          new = lowercodepoint (c);

        if (new is c) goto theend;
        char_from_code (new, buf);
        goto setaction;
      } else
        goto theend;
    }

    buf[0] = CASE_B[p-CASE_A];
  }

  buf[1] = '\0';

setaction:
  action = AllocType (action);
  act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  My(String).replace_numbytes_at_with ($mycur(data), bytelen (buf), $mycur(cur_col_idx), buf);
  stack_push (action, act);
  vundo_push (this, action);
  self(draw_cur_row);

theend:
  $my(flags) |= BUF_IS_MODIFIED;
  ved_normal_right (this, 1, 1);
  return DONE;
}

private int ved_indent (buf_t *this, int count, utf8 com) {
  if (com is '<') {
    if ($mycur(data)->num_bytes is 0 or (IS_SPACE ($mycur(data)->bytes[0]) is 0))
      return NOTHING_TODO;
  } else
    if (str_eq (VISUAL_MODE_LW, $my(mode)))
      ifnot ($mycur(data)->num_bytes) return NOTHING_TODO;

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);

  int shiftwidth = ($my(ftype)->shiftwidth ? $my(ftype)->shiftwidth : DEFAULT_SHIFTWIDTH);

  if ((shiftwidth * count) > $my(dim)->num_cols)
    count = $my(dim)->num_cols / shiftwidth;

  if (com is '>') {
    int len = shiftwidth * count;
    char s[len + 1];
    int i;
    for (i = 0; i < len; i++) { s[i] = ' '; } s[i] = '\0';
    My(String).prepend ($mycur(data), s);
  } else {
    char *s = $mycur (data)->bytes;
    int i = 0;
    while (IS_SPACE (*s) and i < shiftwidth * count) {
      s++; i++;
    }

    My(String).clear ($mycur(data));
    My(String).append ($mycur(data), s);
    if ($mycur(cur_col_idx) >= (int) $mycur(data)->num_bytes) {
      $mycur(cur_col_idx) = $mycur(first_col_idx) = 0;
      $my(video)->col_pos = $my(cur_video_col) = 1;
      ved_normal_eol (this);
    }
  }

  stack_push (action, act);
  vundo_push (this, action);
  $my(flags) |= BUF_IS_MODIFIED;
  self(draw_cur_row);
  return DONE;
}

private int ved_normal_Yank (buf_t *this, int count, int regidx) {
  if (count > this->num_items - this->cur_idx)
    count = this->num_items - this->cur_idx;

  rg_t *rg = &$my(regs)[regidx];
  My(Ed).free_reg ($my(root), rg);

  rg->reg = regidx;
  rg->type = LINEWISE;

  int currow_idx = this->cur_idx;

  for (int i = 0; i < count; i++) {
    reg_t *reg = AllocType (reg);
    self(cur.set, (currow_idx + count - 1) - i);
    reg->data = My(String).new_with ($mycur(data)->bytes);
    rg->cur_col_idx = $mycur(cur_col_idx);
    rg->first_col_idx = $mycur(first_col_idx);
    rg->col_pos = $my(cur_video_col);
    stack_push (rg, reg);
  }

  VED_MSG("yanked [linewise] into register [%c]", REGISTERS[regidx]);
  return DONE;
}

private int ved_normal_yank (buf_t *this, int count, int regidx) {
  if (count > (int) $mycur(data)->num_bytes - $mycur(cur_col_idx))
    count = $mycur(data)->num_bytes - $mycur(cur_col_idx);

  rg_t *rg = &$my(regs)[regidx];
  My(Ed).free_reg ($my(root), rg);
  rg->reg = regidx;
  rg->type = CHARWISE;
  reg_t *reg = AllocType (reg);
  char buf[count + 1];
  char *bytes = $mycur(data)->bytes + $mycur(cur_col_idx);

  int bufidx = 0;
  for (int i = 0; i < count and *bytes; i++) {
    int clen = char_byte_len (*bytes);
    loop (clen) buf[bufidx + $i] = bytes[$i];
    bufidx += clen;
    bytes += clen;
    i += clen - 1;
  }

  buf[bufidx] = '\0';

  reg->data = My(String).new_with (buf);
  stack_push (rg, reg);

  VED_MSG("yanked [charwise] into register [%c]", REGISTERS[regidx]);
  return DONE;
}

private int ved_normal_put (buf_t *this, int regidx, utf8 com) {
  rg_t *rg = &$my(regs)[regidx];
  reg_t *reg = rg->head;

  if (NULL is reg) return NOTHING_TODO;

  action_t *action = AllocType (action);

  row_t *currow = this->current;
  int currow_idx = this->cur_idx;

  if (com is 'P') {
    reg->prev = NULL;
    for (;;) {
      if (NULL is reg->next)
        break;

      reg_t *t = reg; reg = reg->next; reg->prev = t;
    }
  }

  int linewise_num = 0;

  while (reg isnot NULL) {
    act_t *act = AllocType (act);
    if (rg->type is LINEWISE) {
      row_t *row = self(row.new_with, reg->data->bytes);
      vundo_set (act, INSERT_LINE);
      linewise_num++;

      if ('p' is com)
        current_list_append (this, row);
      else
        current_list_prepend (this, row);

      act->idx = this->cur_idx;
    } else {
      vundo_set (act, REPLACE_LINE);
      act->idx = this->cur_idx;
      act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
      My(String).insert_at ($mycur(data), reg->data->bytes, $mycur(cur_col_idx) +
        (('P' is com or 0 is $mycur(data)->num_bytes) ? 0 : char_byte_len ($mycur(data)->bytes[$mycur(cur_col_idx)])));
    }

    stack_push (action, act);
    if (com is 'P')
      reg = reg->prev;
    else
      reg = reg->next;
  }

  vundo_push (this, action);

  if (rg->type is LINEWISE) {

    buf_adjust_marks (this, INSERT_LINE, this->cur_idx, this->cur_idx + linewise_num);

    if ('p' is com) {
      this->current = currow;
      this->cur_idx = currow_idx;
    } else {
      if ($my(video_first_row_idx) is this->cur_idx)
        $my(video_first_row) = this->current;

      $mycur(cur_col_idx) = rg->cur_col_idx;
      $mycur(first_col_idx) = rg->first_col_idx;
      $my(video)->col_pos = $my(cur_video_col) = rg->col_pos;
    }
  }

  $my(flags) |= BUF_IS_MODIFIED;
  self(draw);
  return DONE;
}

private int ved_cmd_delete (buf_t *this, int count, int reg) {
  utf8 c = My(Input).get ($my(term_ptr));
  switch (c) {
    case 'G':
    case END_KEY:
      count = (this->num_items - 1) - this->cur_idx;
      break;
    case 'g':
    case HOME_KEY:
      count = this->cur_idx + 1;
      ved_normal_bof (this);
  }

  switch (c) {
    case 'G':
    case END_KEY:
    case 'g':
    case HOME_KEY:
    case 'd':
      return ved_delete_line (this, count, reg);

    default:
      return NOTHING_TODO;
   }
}

private int insert_change_line (buf_t *this, utf8 c, action_t **action) {
  if ($mycur(data)->num_bytes) RM_TRAILING_NEW_LINE;

  if (c is ARROW_UP_KEY) ved_normal_up (this, 1, 1, 1);
  else if (c is ARROW_DOWN_KEY) ved_normal_down (this, 1, 1, 1);
  else if (c is PAGE_UP_KEY) ved_normal_page_up (this, 1);
  else if (c is PAGE_DOWN_KEY) ved_normal_page_down (this, 1);
  else {
    int isatend = $mycur(cur_col_idx) is (int) $mycur(data)->num_bytes;
    if (isatend) {
      self(cur.append_with, $my(ftype)->autoindent (this, this->current)->bytes);
    } else {
      char bytes[MAXLINE];
      int len = $mycur(data)->num_bytes - $mycur(cur_col_idx);
      memcpy (bytes, $mycur(data)->bytes + $mycur(cur_col_idx), len);
      bytes[len] = '\0';
      My(String).clear_at ($mycur(data), $mycur(cur_col_idx));
      self(cur.append_with, str_fmt ("%s%s",
        $my(ftype)->autoindent (this, this->current)->bytes,
        bytes));
    }

    this->current = this->current->prev;
    this->cur_idx--;
    buf_adjust_marks (this, INSERT_LINE, this->cur_idx, this->cur_idx + 1);

    act_t *act = AllocType (act);
    vundo_set (act, INSERT_LINE);

    $mycur(cur_col_idx) = 0;
    ved_normal_down (this, 1, 0, 0);
    ADD_TRAILING_NEW_LINE;
    ved_normal_right (this, $my(shared_int) + isatend, 0);

    act->idx = this->cur_idx;
    stack_push (*action, act);
    $my(flags) |= BUF_IS_MODIFIED;
    self(draw);
    return DONE;
  }

  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);
  stack_push (*action, act);
  $my(flags) |= BUF_IS_MODIFIED;
  return DONE;
}

private int ved_insert_character (buf_t *this, utf8 *c) {
  int has_pop_pup = 1;
  *c = BUF_GET_AS_NUMBER (has_pop_pup, $my(video)->row_pos - 1,
      $my(video)->col_pos + 1, $my(video)->num_cols, "utf8 code:");
  ifnot (*c) return NOTHING_TODO;
  $my(state) |= ACCEPT_TAB_WHEN_INSERT;
  return NEWCHAR;
}

private int ved_insert_handle_ud_line_completion (buf_t *this, utf8 *c) {
  char *line;
  int len = 0;
  int up_or_down_line = *c is CTRL('y');

  if (up_or_down_line)
    ifnot (this->num_items)
      return NOTHING_TODO;
    else {
      line = $mycur(prev)->data->bytes;
      len =  $mycur(prev)->data->num_bytes;
    }
  else
    if (this->cur_idx is this->num_items - 1)
      return NOTHING_TODO;
    else {
      line = $mycur(next)->data->bytes;
      len =  $mycur(next)->data->num_bytes;
    }

  if (len < $mycur(cur_col_idx) or len is 0)
    return NOTHING_TODO;

  int nolen = 0 is $mycur(data)->num_bytes;
  int nth = 0;
  if (nolen)
    nth = 1;
  else
    nth = char_is_nth_at ($mycur(data)->bytes, $mycur(cur_col_idx) -
      ($mycur(cur_col_idx) is (int) $mycur(data)->num_bytes and
      ($mycur(data)->bytes[$mycur(data)->num_bytes - 1] is '\n' or
       $mycur(data)->bytes[$mycur(cur_col_idx)] is 0)),
       $mycur(data)->num_bytes);

  if (nolen is 0) /* wrong ugly code but effective (in this case and time) */
    if ($mycur(data)->bytes[$mycur(cur_col_idx)] is 0) nth++;

  *c = char_get_nth_code (line, nth, len);

  if (*c is 0)
    return NOTHING_TODO;

  return NEWCHAR;
}

private int ved_insert_last_insert (buf_t *this) {
  if ($my(last_insert)->num_bytes is 0) return NOTHING_TODO;
  My(String).insert_at ($mycur(data), $my(last_insert)->bytes, $mycur(cur_col_idx));
  ved_normal_right (this, char_num ($my(last_insert)->bytes, $my(last_insert)->num_bytes), 1);
  self(draw_cur_row);
  return DONE;
}

typedef void (*draw_buf) (buf_t *);
typedef void (*draw_cur_row) (buf_t *);

private void buf_draw_void (buf_t *this) {(void) this;}
private void buf_draw_cur_row_void (buf_t *this) {(void) this;}

#define VISUAL_ADJUST_IDXS(vis__)           \
do {                                        \
  if ((vis__).fidx > (vis__).lidx) {        \
    int t = (vis__).fidx;                   \
    (vis__).fidx = (vis__).lidx;            \
    (vis__).lidx = t;                       \
  }                                         \
} while (0)

#define VISUAL_RESTORE_STATE(vis__, mark__) \
   VISUAL_ADJUST_IDXS(vis__);               \
   state_restore (&(mark__));               \
   self(cur.set, (mark__).cur_idx);         \
   this->cur_idx = (mark__).cur_idx

#define VISUAL_ADJUST_COL(idx)                                                        \
({                                                                                    \
  int nth_ = char_is_nth_at ($mycur(data)->bytes, (idx), $mycur(data)->num_bytes);    \
  if (nth_ isnot -1) {                                                                \
    ved_normal_bol (this);                                                            \
    ved_normal_right (this, nth_ - 1, 1);                                             \
  }                                                                                   \
})

#define VISUAL_INIT_FUN(fmode, parse_fun)                                             \
  buf_set_draw_topline (this);                                                        \
  char prev_mode[bytelen ($my(mode)) + 1];                                            \
  memcpy (prev_mode, $my(mode), sizeof (prev_mode));                                  \
  strcpy ($my(mode), (fmode));                                                        \
  mark_t mark; state_set (&mark); mark.cur_idx = this->cur_idx;                       \
  draw_buf dbuf = $self(draw);                                                        \
  draw_cur_row drow = $self(draw_cur_row);                                            \
  $self(draw) = buf_draw_cur_row_void;                                                \
  $self(draw_cur_row) = buf_draw_cur_row_void;                                        \
  int reg = -1;  int count = 1;  (void) count;                                      \
  $my(vis)[1] = (vis_t) {                                                         \
    .fidx = this->cur_idx, .lidx = this->cur_idx, .orig_syn_parser = $my(syn)->parse};                                  \
 $my(vis)[0] = (vis_t) {                                                          \
    .fidx = $mycur(cur_col_idx), .lidx = $mycur(cur_col_idx), .orig_syn_parser = $my(syn)->parse};\
  $my(syn)->parse = (parse_fun)

#define VIS_HNDL_CASE_REG(reg)                                           \
  case '"':                                                              \
    if (-1 isnot (reg)) goto theend;                                     \
                                                                         \
    (reg) = My(Input).get ($my(term_ptr));                               \
    {                                                                    \
    char *r = strchr (REGISTERS, reg);                                   \
    (reg) = (NULL is strchr (REGISTERS, (reg))) ? -1 : r - REGISTERS;    \
    }                                                                    \
                                                                         \
    continue

#define VIS_HNDL_CASE_INT(count)                                         \
  case '1'...'9':                                                        \
    {                                                                    \
      char intbuf[8];                                                    \
      intbuf[0] = c;                                                     \
      int idx = 1;                                                       \
      c = BUF_GET_NUMBER (intbuf, idx);                                  \
      if (idx is MAX_COUNT_DIGITS) goto handle_char;                     \
      intbuf[idx] = '\0';                                                \
      count = atoi (intbuf);                                             \
                                                                         \
      goto handle_char;                                                  \
    }                                                                    \
                                                                         \
    continue

private char *ved_syn_parse_visual_lw (buf_t *this, char *line, int len, int idx, row_t *row) {
  (void) len;

  if ((idx is $my(vis)[0].fidx) or
      (idx > $my(vis)[0].fidx and $my(vis)[0].fidx < $my(vis)[0].lidx and
       idx <= $my(vis)[0].lidx) or
      (idx < $my(vis)[0].fidx and $my(vis)[0].fidx > $my(vis)[0].lidx and
       idx >= $my(vis)[0].lidx) or
      (idx > $my(vis)[0].lidx and $my(vis)[0].lidx < $my(vis)[0].fidx and
       idx < $my(vis)[0].fidx)) {
    char *s = str_fmt ("%s%s%s", TERM_MAKE_COLOR(HL_VISUAL), line,
       TERM_COLOR_RESET);
    line[0] = '\0';
    strncpy (line, s, len + TERM_SET_COLOR_FMT_LEN + TERM_COLOR_RESET_LEN + 1);
    return line;
  }

  return $my(vis)[0].orig_syn_parser (this, line, len, idx, row);
}

private int ved_normal_visual_lw (buf_t *this) {
  VISUAL_INIT_FUN (VISUAL_MODE_LW, ved_syn_parse_visual_lw);
  $my(vis)[0] = $my(vis)[1];

  for (;;) {
    $my(vis)[0].lidx = this->cur_idx;
    dbuf (this);

    utf8 c = My(Input).get ($my(term_ptr));

handle_char:
    switch (c) {
      VIS_HNDL_CASE_REG(reg);
      VIS_HNDL_CASE_INT(count);

      case ARROW_DOWN_KEY:
        ved_normal_down (this, 1, 0, 1);
        continue;

      case ARROW_UP_KEY:
        ved_normal_up (this, 1, 1, 1);
        continue;

      case PAGE_DOWN_KEY:
        ved_normal_page_down (this, 1);
        continue;

      case PAGE_UP_KEY:
        ved_normal_page_up (this, 1);
        continue;

      case ESCAPE_KEY:
        VISUAL_RESTORE_STATE ($my(vis)[1], mark);
        goto theend;

      case HOME_KEY:
        ved_normal_bof (this);
        continue;

      case END_KEY:
        ved_normal_eof (this);
        continue;

      case '>':
      case '<':
        if ($my(vis)[0].fidx <= $my(vis)[0].lidx) {
          VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        } else {
          VISUAL_ADJUST_IDXS($my(vis)[0]);
          self(cur.set, $my(vis)[0].fidx);
          this->cur_idx = $my(vis)[0].fidx;
        }

        {
          action_t *action = AllocType (action);

          for (int i = $my(vis)[0].fidx; i <= $my(vis)[0].lidx; i++) {
            if (DONE is ved_indent (this, count, c)) {
              action_t *laction = vundo_pop (this);
              act_t *act = stack_pop (laction, act_t);
              stack_push (action, act);
              free (laction);
            }

            this->current = this->current->next;
            this->cur_idx++;
          }

          vundo_push (this, action);

          VISUAL_RESTORE_STATE ($my(vis)[1], mark);
          if (c is '<' and $mycur(cur_col_idx) >= (int) $mycur(data)->num_bytes - 1) {
            ved_normal_noblnk (this);
            VISUAL_ADJUST_COL ($mycur(cur_col_idx));
          }
        }

        goto theend;

      case 'd':
      case 'y':
      case 'Y':
        if ($my(vis)[0].fidx <= $my(vis)[0].lidx) {
          VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        } else {
          VISUAL_ADJUST_IDXS($my(vis)[0]);
          self(cur.set, $my(vis)[0].fidx);
          this->cur_idx = $my(vis)[0].fidx;
        }

        if (-1 is reg) reg = REG_UNAMED;

        if (c is 'd')
          ved_delete_line (this, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, reg);
        else {
          ved_normal_Yank (this, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, reg);
          VISUAL_RESTORE_STATE ($my(vis)[1], mark);
        }

        goto theend;

      case 's':
        VISUAL_ADJUST_IDXS($my(vis)[0]);
        {
        rline_t *rl = ved_rline_new ($my(root), $my(term_ptr), My(Input).get, *$my(prompt_row_ptr),
          $my(dim)->num_cols, $my(video));
        string_t *str = My(String).new_with_fmt ("substitute --range=%d,%d --global -i --pat=",
            $my(vis)[0].fidx + 1, $my(vis)[0].lidx + 1);
        BYTES_TO_RLINE (rl, str->bytes, (int) str->num_bytes);
        rl->state |= (RL_WRITE|RL_BREAK); rline_edit (rl);
        ved_rline (&this, rl);
        My(String).free (str);
        }
        goto theend;

      case 'w':
        VISUAL_ADJUST_IDXS($my(vis)[0]);
        {
        rline_t *rl = ved_rline_new ($my(root), $my(term_ptr), My(Input).get, *$my(prompt_row_ptr),
          $my(dim)->num_cols, $my(video));
        string_t *str = My(String).new_with_fmt ("write --range=%d,%d ",
            $my(vis)[0].fidx + 1, $my(vis)[0].lidx + 1);
        BYTES_TO_RLINE (rl, str->bytes, (int) str->num_bytes);
        rl->state |= (RL_WRITE|RL_BREAK); rline_edit (rl);
        ved_rline (&this, rl);
        My(String).free (str);
        }
        goto theend;

      default:
        continue;
    }
  }

theend:
  $my(syn)->parse = $my(vis)[0].orig_syn_parser;
  $self(draw) = dbuf;
  $self(draw_cur_row) = drow;
  strcpy ($my(mode), prev_mode);

  self(draw);

  return DONE;
}

private char *ved_syn_parse_visual_line (buf_t *this, char *line, int len, row_t *currow) {
  ifnot (len) return line;

  int fidx = $my(vis)[0].fidx;
  int lidx = $my(vis)[0].lidx;

  if (currow->first_col_idx) {
    if (currow->first_col_idx > fidx) fidx = 0;
    else fidx -= currow->first_col_idx;
  }
  if (lidx >= len) lidx = lidx - currow->first_col_idx;
  if (fidx > lidx)
    {int t = fidx; fidx = lidx; lidx = t;}

  int cclen = char_byte_len (line[lidx]);
  int fpart_len = fidx,
      context_len = (lidx - (currow->first_col_idx ? 2 : fidx)) + cclen,
      lpart_len = len - fpart_len - context_len;

  if (lpart_len < 0) lpart_len = 0;
  char fpart[fpart_len + 1]; char context[context_len + 1]; char lpart[lpart_len + 1];
  strncpy (fpart, line, fpart_len);
  fpart[fpart_len] = '\0';
  strncpy (context, line + fpart_len, context_len);
  context[context_len] = '\0';
  strncpy (lpart, line + fpart_len + context_len, lpart_len);
  lpart[lpart_len] = '\0';

  char *s = str_fmt ("%s%s%s%s%s", fpart, TERM_MAKE_COLOR(HL_VISUAL), context,
     TERM_COLOR_RESET, lpart);
  line[0] = '\0';
  strncpy (line, s, len + TERM_SET_COLOR_FMT_LEN + TERM_COLOR_RESET_LEN + 1);
  return line;
}

private char *ved_syn_parse_visual_cw (buf_t *this, char *line, int len, int idx, row_t *row) {
  (void) idx;
  return ved_syn_parse_visual_line (this, line, len, row);
}

private int ved_normal_visual_cw (buf_t *this) {
  VISUAL_INIT_FUN (VISUAL_MODE_CW, ved_syn_parse_visual_cw);

  $my(vis)[1] = $my(vis)[0];
  $my(vis)[0] = (vis_t) {.fidx = $mycur(cur_col_idx), .lidx = $mycur(cur_col_idx),
     .orig_syn_parser = $my(vis)[1].orig_syn_parser};

  for (;;) {
    $my(vis)[0].lidx = $mycur(cur_col_idx);
    drow (this);
    utf8 c = My(Input).get ($my(term_ptr));

handle_char:
    switch (c) {
      VIS_HNDL_CASE_REG(reg);
      VIS_HNDL_CASE_INT(count);

      case ARROW_LEFT_KEY:
        ved_normal_left (this, 1);
        continue;

      case ARROW_RIGHT_KEY:
        ved_normal_right (this, 1, 1);
        continue;

      case ESCAPE_KEY:
        goto theend;

      case HOME_KEY:
        ved_normal_bol (this);
        continue;

      case END_KEY:
        ved_normal_eol (this);
        continue;

      case 'd':
      case 'x':
      case 'y':
      case 'Y':
        if (-1 is reg) reg = REG_UNAMED;

        if (c is 'd' or c is 'x') {
          if ($my(vis)[0].lidx < $my(vis)[0].fidx) {
            VISUAL_ADJUST_IDXS($my(vis)[0]);
          } else {   /* MACRO BLOCKS ARE EVIL */
            VISUAL_RESTORE_STATE ($my(vis)[0], mark);
          }
          ved_normal_delete (this, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, reg);
        } else {
          if ($my(vis)[0].lidx < $my(vis)[0].fidx) {
            VISUAL_ADJUST_IDXS($my(vis)[0]);
          } else {   /* MACRO BLOCKS ARE EVIL */
            VISUAL_RESTORE_STATE ($my(vis)[0], mark);
          }
          ved_normal_yank (this, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, reg);
        }

        goto theend;
      default:
        continue;

    }
  }

theend:
  $my(syn)->parse = $my(vis)[0].orig_syn_parser;
  $self(draw) = dbuf;
  $self(draw_cur_row) = drow;
  strcpy ($my(mode), prev_mode);

  self(draw);
  return DONE;
}

private char *ved_syn_parse_visual_bw (buf_t *this, char *line, int len, int idx, row_t *row) {
  (void) len;

  if ((idx is $my(vis)[1].fidx) or
      (idx > $my(vis)[1].fidx and $my(vis)[1].fidx < $my(vis)[1].lidx and
       idx <= $my(vis)[1].lidx) or
      (idx < $my(vis)[1].fidx and $my(vis)[1].fidx > $my(vis)[1].lidx and
       idx >= $my(vis)[1].lidx) or
      (idx > $my(vis)[1].lidx and $my(vis)[1].lidx < $my(vis)[1].fidx and
       idx < $my(vis)[1].fidx)) {
    return ved_syn_parse_visual_line (this, line, len, row);
  }

  return $my(vis)[0].orig_syn_parser (this, line, len, idx, row);
}


private int ved_visual_bwise (buf_t *this) {
  VISUAL_INIT_FUN (VISUAL_MODE_BW, ved_syn_parse_visual_bw);

  for (;;) {
    $my(vis)[1].lidx = this->cur_idx;
    $my(vis)[0].lidx = $mycur(cur_col_idx);
    dbuf (this);

    utf8 c = My(Input).get ($my(term_ptr));

handle_char:
    switch (c) {
      VIS_HNDL_CASE_REG(reg);
      VIS_HNDL_CASE_INT(count);

      case ARROW_DOWN_KEY:
        ved_normal_down (this, 1, 1, 1);
        continue;

      case ARROW_UP_KEY:
        ved_normal_up (this, 1, 1, 1);
        continue;

      case PAGE_DOWN_KEY:
        ved_normal_page_down (this, 1);
        continue;

      case PAGE_UP_KEY:
        ved_normal_page_up (this, 1);
        continue;

      case ESCAPE_KEY:
        VISUAL_RESTORE_STATE ($my(vis)[0], mark);
        goto theend;

      case HOME_KEY:
        ved_normal_bof (this);
        continue;

      case END_KEY:
        ved_normal_eof (this);
        continue;

      case ARROW_LEFT_KEY:
        ved_normal_left (this, 1);
        continue;

      case ARROW_RIGHT_KEY:
        ved_normal_right (this, 1, 1);
        continue;

      case 'i':
      case 'I':
      case 'c':
        VISUAL_ADJUST_IDXS($my(vis)[0]);
        VISUAL_ADJUST_IDXS($my(vis)[1]);
        {
          char buf[MAXLINE];
          utf8 cc = 0;
          int len = 0;
          for (;;) {
            cc = My(Input).get ($my(term_ptr));
            if (cc is ESCAPE_KEY) break;
            buf[len++] = cc;
          }

          buf[len] = '\0';

          for (int idx = $my(vis)[1].fidx; idx <= $my(vis)[1].lidx; idx++) {
            self(cur.set, idx);
            VISUAL_ADJUST_COL ($my(vis)[0].fidx);

            if (c is 'c') {
              if ((int) $mycur(data)->num_bytes < $my(vis)[0].fidx)
                continue;
              else
                ved_normal_delete (this, $my(vis)[0].lidx - $my(vis)[0].fidx + 1, REG_RDONLY);
            }

            My(String).insert_at ($mycur(data), buf, $my(vis)[0].fidx);
            $my(flags) |= BUF_IS_MODIFIED;
          }
        }

        VISUAL_RESTORE_STATE ($my(vis)[1], mark);
        goto theend;

      case 'x':
      case 'd':
        if (-1 is reg) reg = REG_UNAMED;
        {
        action_t *action = AllocType (action);
        action_t *baction =AllocType (action);

        VISUAL_ADJUST_IDXS($my(vis)[0]);
        VISUAL_ADJUST_IDXS($my(vis)[1]);

        for (int idx = $my(vis)[1].fidx; idx <= $my(vis)[1].lidx; idx++) {
          self(cur.set, idx);
          VISUAL_ADJUST_COL ($my(vis)[0].fidx);
          if ((int) $mycur(data)->num_bytes < $my(vis)[0].fidx + 1) continue;
          int lidx__ = (int) $mycur(data)->num_bytes < $my(vis)[0].lidx ?
            (int) $mycur(data)->num_bytes : $my(vis)[0].lidx;
          ved_normal_delete (this, lidx__ - $my(vis)[0].fidx + 1, reg);
          action_t *paction = vundo_pop (this);
          act_t *act = stack_pop (paction, act_t);

          stack_push (baction, act);
          free (paction);
          $my(flags) |= BUF_IS_MODIFIED;
        }
        act_t *act = stack_pop (baction, act_t);
        while (act isnot NULL) {
          stack_push (action, act); act = stack_pop (baction, act_t);
        }

        free (baction);
        vundo_push (this, action);
        }

        goto theend;

      default:
        continue;
    }
  }

theend:
  $my(syn)->parse = $my(vis)[0].orig_syn_parser;
  $self(draw) = dbuf;
  $self(draw_cur_row) = drow;
  strcpy ($my(mode), prev_mode);

  self(draw);

  return DONE;
}

private int ved_edit_fname (buf_t **thisp, char *fname, int frame, int force, int draw) {
  buf_t *this = *thisp;

  if (fname isnot NULL) {
    if ($my(at_frame) is frame) $my(is_visible) = 0;

    buf_t *that = My(Win).buf_new ($my(parent), fname, frame);
    current_list_set (that, 0);

    int idx = My(Win).append_buf ($my(parent), that);
    My(Win).set.current_buf ($my(parent), idx);

    *thisp = that;
     this = that;

    if (draw) self(draw);
    goto theend;
  }

  ifnot (force) return NOTHING_TODO;

  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);

  self(cur.set, 0);
  ved_delete_line (this, this->num_items, REG_UNAMED);
 // current_list_pop (this, row_t);
  self(read.fname);
  if (draw) self(draw);
    /* todo: reload */
  return DONE;

theend:
  return DONE;
}

private int ved_split (buf_t **thisp, char *fname) {
  buf_t *this = *thisp;

  if (win_add_frame ($my(parent)) is NOTHING_TODO)
    return NOTHING_TODO;

  ved_edit_fname (thisp, fname, $myparents(num_frames) - 1, 1, 0);

  $myparents(cur_frame) = $myparents(num_frames) - 1;

  My(Win).draw ($my(parent));
  return DONE;
}

private int ved_enew_fname (buf_t **thisp, char *fname) {
  buf_t *this = *thisp;
  win_t *w = My(Ed).win.new ($my(root), NULL, 1);
  $my(root)->prev_idx = $my(root)->cur_idx;
  My(Ed).append.win ($my(root), w);
  $my(parent) = $my(root)->current;
  ved_edit_fname (thisp, fname, 0, 1, 1);
  return DONE;
}

private int ved_win_change (buf_t **thisp, int com, char *name, int accept_rdonly) {
  buf_t *this = *thisp;

  if ($my(root)->num_items is 1) return NOTHING_TODO;

  int idx = $my(root)->cur_idx;
  int cidx = idx;

  ifnot (NULL is name) {
    int found = 0;
    win_t *it = $my(root)->head;
    idx = 0;
    while (it) {
      if (str_eq (it->prop->name, name)) {
        found = 1;
        break;
      }
      idx++;
      it = it->next;
    }
    ifnot (found) return NOTHING_TODO;
  } else {
change_idx:
    switch (com) {
      case VED_COM_WIN_CHANGE_PREV:
        if (--idx is -1) { idx = $my(root)->num_items - 1; } break;
      case VED_COM_WIN_CHANGE_NEXT:
        if (++idx is $my(root)->num_items) { idx = 0; } break;
      case VED_COM_WIN_CHANGE_PREV_FOCUSED:
        idx = $my(root)->prev_idx;
    }
    if (idx is cidx) return NOTHING_TODO;

    ifnot (accept_rdonly) {
      win_t *w = My(Ed).get.win_by_idx ($my(root), idx);
      if (NULL is w) return NOTHING_TODO;
      if (str_eq (w->prop->name, VED_SPECIAL_WIN)) {
        if (com is VED_COM_WIN_CHANGE_PREV_FOCUSED)
          com = VED_COM_WIN_CHANGE_PREV;

        goto change_idx;
      }
    }
  }

  My(Ed).set.current_win ($my(root), idx);
  this = $my(root)->current->current;
  $my(parent) = $my(root)->current;
  *thisp = My(Win).set.current_buf ($my(parent), $my(parent)->cur_idx);
  My(Win).set.video_dividers ($my(parent));
  My(Win).draw ($my(parent));
  return DONE;
}

private int ved_win_delete (ed_t *root, buf_t **thisp) {
  if (1 is root->num_items) return EXIT;
  win_t * tmp = current_list_pop (root, win_t);
  win_free (tmp);

  buf_t *this = root->current->current;
  $my(parent) = root->current;
  *thisp = My(Win).set.current_buf ($my(parent), $my(parent)->cur_idx); 
  My(Win).set.video_dividers ($my(parent));
  My(Win).draw ($my(parent));
  return DONE;
}

private int ved_write_to_fname (buf_t *this, char *fname, int append, int fidx, int lidx, int force) {
  if (NULL is fname) return NOTHING_TODO;
  int retval = NOTHING_TODO;

  string_t *fnstr = string_new_with (fname);
  ifnot (fnstr->num_bytes) goto theend;

  if (fnstr->bytes[fnstr->num_bytes - 1] is '%' and (NULL isnot $my(basename))) {
    My(String).clear_at(fnstr, fnstr->num_bytes - 1);
    My(String).append (fnstr, $my(basename));
  }

  int fexists = file_exists (fnstr->bytes);
  if (fexists and 0 is append and 0 is force) {
    VED_MSG_ERROR(MSG_FILE_EXISTS_AND_NO_FORCE, fnstr->bytes);
    goto theend;
  }

  ifnot (fexists) append = 0;

  FILE *fp = fopen (fnstr->bytes, (append ? "a+" : "w"));
  if (NULL is fp) {
    SYS_MSG_ERROR (errno);
    goto theend;
  }

  int idx = 0;
  row_t *it = this->head;
  while (idx++ < fidx) it = it->next;
  idx--;

  size_t bts = 0;
  while (idx++ <= lidx) {
    bts += fprintf (fp, "%s\n", it->data->bytes);
    it = it->next;
  }

  fclose (fp);
  VED_MSG("%s: %zd bytes written%s", fnstr->bytes, bts, (append ? " [appended]" : " "));

  retval = DONE;

theend:
  My(String).free (fnstr);
  return retval;
}

private int ved_write_buffer (buf_t *this, int force) {
  if (str_eq ($my(fname), UNAMED)) {
    VED_MSG_ERROR (MSG_CAN_NOT_WRITE_AN_UNAMED_BUFFER);
    return NOTHING_TODO;
  }

  if ($my(flags) & BUF_IS_RDONLY) {
    VED_MSG_ERROR(MSG_BUF_IS_READ_ONLY);
    return NOTHING_TODO;
  }

  ifnot ($my(flags) & BUF_IS_MODIFIED) {
    ifnot (force) {
      VED_MSG_ERROR (MSG_ON_WRITE_BUF_ISNOT_MODIFIED_AND_NO_FORCE);
      return NOTHING_TODO;
    }
  }

  if ($my(flags) & FILE_EXISTS) {
    struct stat st;
    if (NOTOK is stat ($my(fname), &st)) {
      $my(flags) &= ~FILE_EXISTS;
      utf8 chars[] = {'y', 'Y', 'n', 'N'};
      utf8 c = quest (this, str_fmt (
          "[Warning]\n%s: removed from filesystem since last operation\n"
          "continue writing? [yY|nN]", $my(fname)), chars, ARRLEN (chars));
      switch (c) {case 'n': case 'N': return NOTHING_TODO;};
    } else {
      if ($my(st).st_mtim.tv_sec isnot st.st_mtim.tv_sec) {
        utf8 chars[] = {'y', 'Y', 'n', 'N'};
        utf8 c = quest (this, str_fmt (
            "[Warning]%s: has been modified since last operation\n"
            "continue writing? [yY|nN]", $my(fname)), chars, ARRLEN (chars));
        switch (c) {case 'n': case 'N': return NOTHING_TODO;};
      }
    }
  }

  FILE *fp = fopen ($my(fname), "w");
  if (NULL is fp) {
    SYS_MSG_ERROR(errno);
    return NOTHING_TODO;
  }

  row_t *row = this->head;

  size_t bts = 0;
  while (row isnot NULL) {
    bts += fprintf (fp, "%s\n", row->data->bytes);
    row = row->next;
  }

  $my(flags) &= ~BUF_IS_MODIFIED;
  $my(flags) |= (FILE_IS_READABLE|FILE_IS_WRITABLE|FILE_EXISTS);

  fstat (fileno (fp), &$my(st));
  fclose (fp);
  VED_MSG("%s: %zd bytes written", $my(fname), bts);
  return DONE;
}

private int ved_buf_read (buf_t *this, char *fname) {
  ifnot (file_exists (fname)) return NOTHING_TODO;
  ifnot (file_is_reg (fname)) return NOTHING_TODO;
  if (0 isnot access (fname, R_OK)) return NOTHING_TODO;
  mark_t t;  state_set (&t);  t.cur_idx = this->cur_idx;
  row_t *row = this->current;
  FILE *fp = fopen (fname, "r");
  if (NULL is fp) {
    SYS_MSG_ERROR(errno);
    return NOTHING_TODO;
  }

  action_t *action = AllocType (action);

  int t_len = 0;
  char *line = NULL;
  size_t len = 0;
  ssize_t nread;

  while (-1 != (nread = getline (&line, &len, fp))) {
    if (nread and (line[nread-1] is '\n' or line[nread-1] is '\r'))
      line[nread-1] = '\0';

    act_t *act = AllocType (act);
    vundo_set (act, INSERT_LINE);

    buf_current_append_with (this, line);
    t_len += nread;

    act->idx = this->cur_idx;

    stack_push (action, act);
  }

  fclose (fp);

  ifnot (t_len)
    free (action);
  else
    vundo_push (this, action);

  ifnot (NULL is line) free (line);

  $my(flags) |= BUF_IS_MODIFIED;
  state_restore (&t);
  this->current = row; this->cur_idx = t.cur_idx;
  self(draw);
  return DONE;
}

private int ved_buf_change_bufname (buf_t **thisp, char *bufname) {
  buf_t *this = *thisp;
  int idx;
  buf_t *buf = My(Win).get.buf_by_fname ($my(parent), bufname, &idx);
  if (NULL is buf) return NOTHING_TODO;

  *thisp = My(Win).set.current_buf ($my(parent), idx);
  return DONE;
}

private int ved_buf_change (buf_t **thisp, int com) {
  buf_t *this = *thisp;

  if (1 is $my(parent)->num_items) return NOTHING_TODO;

  int cur_frame = $myparents(cur_frame);
  int idx = $my(parent)->cur_idx;

  switch (com) {
    case VED_COM_BUF_CHANGE_PREV_FOCUSED:
      idx = $my(parent)->prev_idx;
      if (idx >= $my(parent)->num_items)
        idx = $my(parent)->num_items - 1 -
            ($my(parent)->cur_idx is $my(parent)->num_items - 1);
      goto change;

    case VED_COM_BUF_CHANGE_PREV:
      {
        buf_t *it = $my(parent)->current->prev;
        int index = idx;
        while (it) {
          index--;
          if (it->prop->at_frame is cur_frame) {
            idx = index;
            goto change;
          }
          it = it->prev;
        }

        index = $my(parent)->num_items;
        it = $my(parent)->tail;
        while (it) {
          index--;
          if (index < idx) {
            idx = index;
            goto change;
          }

          if (it->prop->at_frame is cur_frame and idx isnot index) {
            idx = index;
            goto change;
          }

          it = it->prev;
        }

        index = $my(parent)->num_items;
        it = $my(parent)->tail;
        while (it) {
          index--;
          if (index isnot idx) {
            idx = index;
            goto change;
          }
          it = it->prev;
        }
      }
      break;

    case VED_COM_BUF_CHANGE_NEXT:
      {
        buf_t *it = $my(parent)->current->next;
        int index = idx;
        while (it) {
          index++;
          if (it->prop->at_frame is cur_frame) {
            idx = index;
            goto change;
          }
          it = it->next;
        }

        index = -1;
        it = $my(parent)->head;
        while (it) {
          index++;
          if (index > idx) {
            idx = index;
            goto change;
          }

          if (it->prop->at_frame is cur_frame and idx isnot index) {
            idx = index;
            goto change;
          }

          it = it->next;
        }

        index = -1;
        it = $my(parent)->head;
        while (it) {
          index++;
          if (index isnot idx) {
            idx = index;
            goto change;
          }
          it = it->next;
        }
      }
      break;

    default: return NOTHING_TODO;
  }

change:
  *thisp = My(Win).set.current_buf ($my(parent), idx);
  return DONE;
}

private int ved_buf_delete (buf_t **thisp, int idx, int force) {
  buf_t *this = *thisp;
  win_t *parent = $my(parent);

  int cur_idx = parent->cur_idx;
  int at_frame =$my(at_frame);

  buf_t *tmp;
  if (cur_idx is idx) {
    if ($my(flags) & BUF_IS_MODIFIED) {
      ifnot (force) {
        VED_MSG_ERROR(MSG_ON_BD_IS_MODIFIED_AND_NO_FORCE);
        return NOTHING_TODO;
      }
    }

    tmp = current_list_pop (parent, buf_t);
    buf_free (tmp);
    if (parent->num_items is 0) return WIN_EXIT;
  } else {
    current_list_set (parent, idx);
    this = parent->current;
    if ($my(flags) & BUF_IS_MODIFIED) {
      ifnot (force) {
        VED_MSG_ERROR(MSG_ON_BD_IS_MODIFIED_AND_NO_FORCE);
        return NOTHING_TODO;
      }
    }

    tmp = current_list_pop (parent, buf_t);
    buf_free (tmp);
    current_list_set (parent, (idx > cur_idx) ? cur_idx : cur_idx - 1);
  }

  int found = 0;
  this = parent->head;
  while (this isnot NULL) {
    if ($my(at_frame) is at_frame) {
      found = 1;
      $my(is_visible) = 1;
      break;
    }

    this = this->next;
  }

  if (found is 0) win_delete_frame (parent, at_frame);

  this = parent->current;

 *thisp = My(Win).set.current_buf (parent, parent->cur_idx);

  if (cur_idx is idx) {
    if (found is 1 or parent->prop->num_frames is 1) {
      ved_buf_change (thisp, VED_COM_BUF_CHANGE_NEXT);
    } else {
      int frame = parent->prop->cur_frame + 1;
      if (frame is parent->prop->num_frames) frame = 0;
      *thisp = win_change_frame (parent, frame);
    }
  }

 if (found is 0) My(Win).draw (parent);

 return DONE;
}

private int ved_complete_digraph_callback (menu_t *menu) {
  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  vstr_t *items = AllocType (vstr);
  char *digraphs[] = {
    "167 §",  "169 ©",  "171 «",  "174 ®",  "176 °",  "178 ²",  "179 ³", "183 ·",
    "185 ¹",  "187 »",  "188 ¼",  "189 ½",  "190 ¾",  "215 ×",  "247 ÷", "729 ˙",
    "8212 —", "8220 “", "8230 …", "8304 ⁰", "8308 ⁴", "8309 ⁵", "8310 ⁶",
    "8311 ⁷", "8312 ⁸", "8313 ⁹", "8314 ⁺", "8315 ⁻", "8316 ⁼", "8317 ⁽",
    "8318 ⁾", "8319 ⁿ", "8364 €", "8531 ⅓", "8532 ⅔", "8533 ⅕", "8534 ⅖",
    "8535 ⅗", "8536 ⅘", "8537 ⅙", "8538 ⅚", "8539 ⅛", "8540 ⅜", "8541 ⅝",
    "8542 ⅞", "8771 ≃", "9833 ♩", "9834 ♪", "9835 ♫", "9836 ♬", "9837 ♭",
    "9838 ♮", "9839 ♯", "10003 ✓"
  };

  for (int i = 0; i < (int) ARRLEN (digraphs); i++)
    if (menu->patlen) {
      ifnot (str_cmp_n (digraphs[i], menu->pat, menu->patlen))
        vstr_append_current_with (items, digraphs[i]);
    } else
      vstr_append_current_with (items, digraphs[i]);

  menu->list = items;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  return DONE;
}

private int ved_complete_digraph (buf_t *this, utf8 *c) {
  int retval = DONE;
  menu_t *menu = menu_new ($my(root), $my(video)->row_pos, *$my(prompt_row_ptr) - 2,
    $my(video)->col_pos, ved_complete_digraph_callback, NULL, 0);
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;

  char *item = menu_create ($my(root), menu);
  *c = 0;
  if (item isnot NULL)
    while (*item isnot ' ') *c = (10 * *c) + (*item++ - '0');

theend:
  menu_free (menu);
  return NEWCHAR;
}

private int ved_complete_arg (menu_t *menu) {
  buf_t *this = menu->this;

  int com = $my(shared_int);
  char *line = $my(shared_str)->bytes;

  if (VED_COMMANDS[com]->args is NULL and
     (com < VED_COM_BUF_DELETE_FORCE or com > VED_COM_BUF_CHANGE_ALIAS)) {
    menu->state |= MENU_QUIT;
    return NOTHING_TODO;
  }

  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  vstr_t *args = AllocType (vstr);

 int patisopt = (menu->patlen ? str_eq (menu->pat, "--bufname=") : 0);

  if ((com >= VED_COM_BUF_DELETE_FORCE and com <= VED_COM_BUF_CHANGE_ALIAS) or
       patisopt) {
    char *cur_fname = $my(fname);

    buf_t *it = $my(parent)->head;
    while (it) {
      ifnot (str_eq (cur_fname, it->prop->fname)) {
        if (0 is menu->patlen or 1 is patisopt)
          vstr_add_sort_and_uniq (args, it->prop->fname);
        else
          ifnot (str_cmp_n (it->prop->fname, menu->pat, menu->patlen))
            vstr_add_sort_and_uniq (args, it->prop->fname);
      }
      it = it->next;
    }

    goto check_list;
  }

  int i = 0;
  ifnot (menu->patlen) {
    while (VED_COMMANDS[com]->args[i])
      vstr_add_sort_and_uniq (args, VED_COMMANDS[com]->args[i++]);
  } else {
    while (VED_COMMANDS[com]->args[i]) {
      ifnot (str_cmp_n (VED_COMMANDS[com]->args[i], menu->pat, menu->patlen))
        if (NULL is strstr (line, VED_COMMANDS[com]->args[i]))
          vstr_add_sort_and_uniq (args, VED_COMMANDS[com]->args[i]);
      i++;
    }
  }

check_list:
  ifnot (args->num_items) {
    menu->state |= MENU_QUIT;
    vstr_free (args);
    return NOTHING_TODO;
  }

  menu->list = args;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  My(String).replace_with (menu->header, menu->pat);

  return DONE;
}

private int ved_complete_command (menu_t *menu) {
  buf_t *this = menu->this;

  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  vstr_t *coms = AllocType (vstr);
  int i = 0;

  ifnot (menu->patlen) {
    while (VED_COMMANDS[i])
      vstr_add_sort_and_uniq (coms, VED_COMMANDS[i++]->com);
  } else {
    while (VED_COMMANDS[i]) {
      ifnot (strncmp (VED_COMMANDS[i]->com, menu->pat, menu->patlen))
        vstr_add_sort_and_uniq (coms, VED_COMMANDS[i]->com);
      i++;
    }
  }

  ifnot (coms->num_items) {
    menu->state |= MENU_QUIT;
    vstr_free (coms);
    return NOTHING_TODO;
  }

  menu->list = coms;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  My(String).replace_with (menu->header, menu->pat);

  return DONE;
}

private int ved_complete_filename (menu_t *menu) {
  buf_t *this = menu->this;
  char dir[PATH_MAX];
  int joinpath;

  if (menu->state & MENU_FINALIZE)  goto finalize;

  dir[0] = '\0';
  joinpath = 0;

  if (menu->state & MENU_INIT) {
    menu->state &= ~MENU_INIT;
  } else
    menu_free_list (menu);

  char *sp = (menu->patlen is 0) ? NULL : menu->pat + menu->patlen - 1;
  char *end = sp;

  if (NULL is sp) {
    char *cwd = dir_get_current ();
    strcpy (dir, cwd);
    free (cwd);
    end = NULL;
    goto getlist;
  }

  if ('~' is menu->pat[0]) {
    if (menu->patlen is 1) {
      end = NULL;
    } else {
      end = menu->pat + 1;
      if (*end is PATH_SEP) { if (*(end + 1)) end++; else end = NULL; }
    }

    memcpy (dir, $myroots(env)->home_dir->bytes, $myroots(env)->home_dir->num_bytes);

    dir[$myroots(env)->home_dir->num_bytes] = '\0';
    joinpath = 1;
    goto getlist;
  }

  if (is_directory (menu->pat)) {
    strcpy (dir, menu->pat);
    end = NULL;
    joinpath = 1;
    goto getlist;
  }

  if (sp is menu->pat) {
   if (*sp is PATH_SEP) {
      dir[0] = *sp; dir[1] = '\0';
      joinpath = 1;
      end = NULL;
    } else {
      char *cwd = dir_get_current ();
      strcpy (dir, cwd);
      free (cwd);
      end = sp;
    }

    goto getlist;
  }

  if (*sp is PATH_SEP) {
    memcpy (dir, menu->pat, menu->patlen - 1);
    dir[menu->patlen - 1] = '\0';
    end = NULL;
    joinpath = 1;
    goto getlist;
  }

  while (sp > menu->pat and *(sp - 1) isnot PATH_SEP) sp--;
  if (sp is menu->pat) {
    end = sp;
    char *cwd = dir_get_current ();
    strcpy (dir, cwd);
    free (cwd);
    goto getlist;
  }

  end = sp;
  sp = menu->pat;
  int i = 0;
  while (sp < end) dir[i++] = *sp++;
  dir[i] = '\0';
  joinpath = 1;

getlist:;
  int endlen = (NULL is end) ? 0 : bytelen (end);
  dirlist_t *dlist = dirlist (dir);

  if (NULL is dlist) {
    menu->state |= MENU_QUIT;
    return NOTHING_TODO;
  }

  vstr_t *vs = AllocType (vstr);
  vstring_t *it = dlist->list->head;

  $my(shared_int) = joinpath;
  My(String).replace_with ($my(shared_str), dir);

  while (it isnot NULL) {
    if (end is NULL or (0 is str_cmp_n (it->data->bytes, end, endlen))) {
      vstr_add_sort_and_uniq (vs, it->data->bytes);
    }

    it = it->next;
  }

  dlist->free (dlist);

  menu->list = vs;
  menu->state |= (MENU_LIST_IS_ALLOCATED|MENU_REINIT_LIST);
  string_replace_with (menu->header, menu->pat);

  return DONE;

finalize:
  menu->state &= ~MENU_FINALIZE;

  if ($my(shared_int)) {
    if ($my(shared_str)->bytes[$my(shared_str)->num_bytes - 1] is PATH_SEP)
      My(String).clear_at ($my(shared_str), $my(shared_str)->num_bytes - 1);

    int len = menu->patlen + $my(shared_str)->num_bytes + 1;
    char tmp[len + 1];
    snprintf (tmp, len + 1, "%s%c%s", $my(shared_str)->bytes, PATH_SEP, menu->pat);
    My(String).replace_with ($my(shared_str), tmp);
  } else
    My(String).replace_with ($my(shared_str), menu->pat);

  if (is_directory ($my(shared_str)->bytes))
    menu->state |= MENU_REDO;
  else
    menu->state |= MENU_DONE;

  return DONE;
}

private void rline_free_members (rline_t *rl) {
  vstr_free (rl->line);
  arg_t *it = rl->head;
  while (it isnot NULL) {
    arg_t *tmp = it->next;
    string_free (it->data);
    string_free (it->option);
    string_free (it->val);
    free (it);
    it = tmp;
  }
  rl->num_items = 0;
  rl->cur_idx = 0;
}

private void rline_free (rline_t *rl) {
  rline_free_members (rl);
  free (rl);
}

private int rline_history_at_beg (rline_t **rl) {
  switch ((*rl)->c) {
    case ESCAPE_KEY:
    case '\r':
    case ARROW_UP_KEY:
    case ARROW_DOWN_KEY:
    case '\t':
    (*rl)->state |= RL_POST_PROCESS;
    return RL_POST_PROCESS;
  }

  (*rl)->state |= RL_OK;
  return RL_OK;
}

private rline_t *rline_complete_history (rline_t *rl, int *idx, int dir) {
  ed_t *this = rl->ed;
  if ($my(history)->rline->num_items is 0) return rl;

  if (dir is -1) {
     if (*idx is 0)
       *idx = $my(history)->rline->num_items - 1;
     else
        *idx -= 1;
   } else {
      if (*idx is $my(history)->rline->num_items - 1)
        *idx = 0;
      else
        *idx += 1;
   }

  int lidx = 0;
  h_rlineitem_t *it = $my(history)->rline->head;

  while (lidx < *idx) { it = it->next; lidx++; }

  lidx += dir;
  rline_t *lrl = rline_new (this, $my(term), My(Input).get, $my(prompt_row),
    $my(dim)->num_cols, $my(video));

  lrl->prompt_row = rl->first_row - 1;
  lrl->prompt_char = 0;

  int (*at_beg) (rline_t **) = rl->at_beg;
  int (*at_end) (rline_t **) = rl->at_end;
  rl->at_beg = rline_history_at_beg;
  rl->at_end = rline_break;

  goto thecheck;

  lidx += dir;
theiter:
  if (dir is -1) {
    if (it->prev is NULL) {
      lidx = $my(history)->rline->num_items - 1;
      it = $my(history)->rline->tail;
    } else {
      it = it->prev;
      lidx--;
    }
  } else {
    if (it->next is NULL) {
      lidx = 0;
      it = $my(history)->rline->head;
    } else {
      lidx++;
      it = it->next;
    }
  }

thecheck:;
#define __free_strings__ My(String).free (str); My(String).free (cur)

  string_t *str = vstr_join (it->data->line, "");
  string_t *cur = vstr_join (rl->line, "");
  if (cur->bytes[cur->num_bytes - 1] is ' ')
    My(String).clear_at (cur, cur->num_bytes - 1);

  if (lidx is *idx or 0 is cur->num_bytes or $my(history)->rline->num_items is 1 or
     (0 is str_cmp_n (str->bytes, cur->bytes, cur->num_bytes))) {
    __free_strings__;
    goto theinput;
  }

  __free_strings__;
  goto theiter;

theinput:
  rline_free_members (lrl);
  lrl->line = vstr_dup (it->data->line);
  lrl->first_row = it->data->first_row;
  lrl->row_pos = it->data->row_pos;

  lrl->state |= (RL_WRITE|RL_BREAK);
  rline_edit (lrl);

  utf8 c = rline_edit (rl)->c;
  switch (c) {
    case ESCAPE_KEY:
      goto theend;

    case ' ':
    case '\r': goto thesuccess;
    case ARROW_DOWN_KEY: dir = -1; goto theiter;
    case ARROW_UP_KEY: dir = 1; goto theiter;
    default: goto theiter;
  }

thesuccess:
  rline_free_members (rl);
  rl->line = vstr_dup (it->data->line);
  rl->first_row = it->data->first_row;
  rl->row_pos = it->data->row_pos;

theend:
  rline_clear (lrl);
  rline_free (lrl);
  rl->at_beg = at_beg;
  rl->at_end = at_end;

  return rl;
}

private void rline_history_push (rline_t *rl) {
  ed_t *this = rl->ed;
  if ($my(max_num_hist_entries) < $my(history)->rline->num_items) {
    h_rlineitem_t *tmp = list_pop_tail ($my(history)->rline, h_rlineitem_t);
    rline_free (tmp->data);
    free (tmp);
  }

  h_rlineitem_t *hrl = AllocType (h_rlineitem);
  hrl->data = rl;
  current_list_prepend ($my(history)->rline, hrl);
}

private void rline_clear (rline_t *rl) {
  rl->state &= ~RL_CLEAR;
  int row = rl->first_row;
  while (row < rl->prompt_row)
    video_draw_at (rl->cur_video, row++);

  video_set_row_with (rl->cur_video, rl->prompt_row - 1, " ");
  video_draw_at (rl->cur_video, rl->prompt_row);

  if (rl->state & RL_CLEAR_FREE_LINE) {
    vstr_free (rl->line);
    rl->num_items = 0;
    rl->cur_idx = 0;
    rl->line = AllocType (vstr);
    vstring_t *vstr = AllocType (vstring);
    vstr->data = string_new_with (" ");
    current_list_append (rl->line, vstr);
  }

  rl->state &= ~RL_CLEAR_FREE_LINE;
}

private vstring_t *ved_rline_get_command (rline_t *rl) {
  vstring_t *it = rl->line->head;
  char com[MAXCOMLEN]; com[0] = '\0';
  int com_idx = 0;
  while (it isnot NULL and it->data->bytes[0] is ' ') it = it->next;
  while (it isnot NULL and it->data->bytes[0] isnot ' ') {
    for (size_t zi = 0; zi < it->data->num_bytes; zi++)
      com[com_idx++] = it->data->bytes[zi];
    it = it->next;
  }
  com[com_idx] = '\0';
  rl->com = -1;

  int i = 0;
  for (i = 0; i < VED_COM_END; i++) {
    if (str_eq (rl->commands[i]->com, com)) {
      rl->com = i;
      break;
    }
  }

  return it;
}

private int ved_rline_tab_completion (rline_t *rl) {
  ifnot (rl->line->num_items) return RL_OK;
  int retval = RL_OK;
  ed_t *this = rl->ed;
  buf_t *curbuf = this->current->current;

redo:;
  string_t *currline = vstr_join (rl->line, "");
  char *sp = currline->bytes + rl->line->cur_idx;
  char *cur = sp;
  while (sp > currline->bytes and *(sp - 1) isnot ' ') sp--;
  int fidx = sp - currline->bytes;
  char tok[cur - sp + 1];
  int toklen = 0;
  while (sp < cur) tok[toklen++] = *sp++;
  tok[toklen] = '\0';

  int type = 0;

  if (fidx is 0) {
    type |= RL_TOK_COMMAND;
  } else {
    ved_rline_get_command (rl);

    curbuf->prop->shared_int = rl->com;
    My(String).replace_with (curbuf->prop->shared_str, currline->bytes);

    if (-1 isnot (rl->com)) {
      if (tok[0] is '-') {
        type |= RL_TOK_ARG;
        ifnot (NULL is strstr (tok, "="))
          type |= RL_TOK_ARG_OPTION;
      } else {
        if (rl->com >= VED_COM_BUF_DELETE_FORCE and
            rl->com <= VED_COM_BUF_CHANGE_ALIAS)
          type |= RL_TOK_ARG;
        else
          type |= RL_TOK_ARG_FILENAME;
      }
    }
  }

  My(String).free (currline);

  ifnot (type) return retval;

  int (*process_list) (menu_t *) = NULL;
  if (type & RL_TOK_ARG_FILENAME)
    process_list = ved_complete_filename;
  else if (type & RL_TOK_COMMAND)
    process_list = ved_complete_command;
  else if (type & RL_TOK_ARG)
    process_list = ved_complete_arg;

  int orig_len = toklen;

  menu_t *menu = menu_new (this, $my(prompt_row) - 2, $my(prompt_row) - 2, 0,
    process_list, tok, toklen);
  if ((retval = menu->retval) is NOTHING_TODO) goto theend;
  menu->state &= ~RL_IS_VISIBLE;

  char *item;

  for (;;) {
    item = menu_create (this, menu);
    if (NULL is item) goto theend;
    if (menu->state & MENU_QUIT) break;
    if (type & RL_TOK_COMMAND or type & RL_TOK_ARG) break;

    int len = bytelen (item);
    memcpy (menu->pat, item, len);
    menu->pat[len] = '\0';
    menu->patlen = len;

    if (type & RL_TOK_ARG_FILENAME) menu->state |= MENU_FINALIZE;

    if (menu->process_list (menu) is NOTHING_TODO) goto theend;

    if (menu->state & (MENU_REDO|MENU_DONE)) break;
  }

  if (type & RL_TOK_ARG_FILENAME)
    item = curbuf->prop->shared_str->bytes;

  ifnot (type & RL_TOK_ARG_OPTION) {
    current_list_set (rl->line, fidx);
    int lidx = fidx + orig_len;
    while (fidx++ < lidx) {
      vstring_t *tmp = current_list_pop (rl->line, vstring_t);
      string_free (tmp->data);
      free (tmp);
    }
  }

  BYTES_TO_RLINE (rl, item, (int) bytelen (item));

  if (menu->state & MENU_REDO) {
    menu_free (menu);
    goto redo;
  }

theend:
  menu_free (menu);
  return RL_OK;
}

private int rline_call_at_beg (rline_t **rl) {
  (*rl)->state |= RL_OK;
  return RL_OK;
}

private int rline_call_at_end (rline_t **rl) {
  (*rl)->state |= RL_OK;
  return RL_OK;
}

private int rline_break (rline_t **rl) {
  (*rl)->state |= RL_BREAK;
  return RL_BREAK;
}

private int rline_tab_completion (rline_t *rl) {
  (void) rl;
  return NOTHING_TODO;
}

private void ved_deinit_commands (void) {
  if (NULL is VED_COMMANDS) return;
  int i = 0;
  while (VED_COMMANDS[i]) {
    free (VED_COMMANDS[i]->com);
    if (VED_COMMANDS[i]->args isnot NULL) {
      int j = 0;
      while (VED_COMMANDS[i]->args[j] isnot NULL)
        free (VED_COMMANDS[i]->args[j++]);
      free (VED_COMMANDS[i]->args);
      }

    free (VED_COMMANDS[i]);
    i++;
  }

  free (VED_COMMANDS); VED_COMMANDS = NULL;
}

private void ved_init_commands (void) {
  ifnot (NULL is VED_COMMANDS) return;

  char *ved_commands[VED_COM_END + 1] = {
    [VED_COM_BUF_CHANGE_NEXT] = "bufnext",
    [VED_COM_BUF_CHANGE_NEXT_ALIAS] = "bn",
    [VED_COM_BUF_CHANGE_PREV_FOCUSED] = "bufprevfocused",
    [VED_COM_BUF_CHANGE_PREV_FOCUSED_ALIAS] = "b`",
    [VED_COM_BUF_CHANGE_PREV] = "bufprev",
    [VED_COM_BUF_CHANGE_PREV_ALIAS] = "bp",
    [VED_COM_BUF_DELETE_FORCE] = "bufdelete!",
    [VED_COM_BUF_DELETE_FORCE_ALIAS] = "bd!",
    [VED_COM_BUF_DELETE] = "bufdelete",
    [VED_COM_BUF_DELETE_ALIAS] = "bd",
    [VED_COM_BUF_CHANGE] = "buffer",
    [VED_COM_BUF_CHANGE_ALIAS] = "b",
    [VED_COM_EDIT_FORCE] = "edit!",
    [VED_COM_EDIT_FORCE_ALIAS] = "e!",
    [VED_COM_EDIT] = "edit",
    [VED_COM_EDIT_ALIAS] = "e",
    [VED_COM_ENEW] = "enew",
    [VED_COM_MESSAGES] = "messages",
    [VED_COM_QUIT_FORCE] = "quit!",
    [VED_COM_QUIT_FORCE_ALIAS] = "q!",
    [VED_COM_QUIT] = "quit",
    [VED_COM_QUIT_ALIAS] = "q",
    [VED_COM_READ] = "read",
    [VED_COM_READ_ALIAS] = "r",
    [VED_COM_SPLIT] = "split",
    [VED_COM_SUBSTITUTE] = "substitute",
    [VED_COM_SUBSTITUTE_WHOLE_FILE_AS_RANGE] = "s%",
    [VED_COM_SUBSTITUTE_ALIAS] = "s",
    [VED_COM_WIN_CHANGE_NEXT] = "winnext",
    [VED_COM_WIN_CHANGE_NEXT_ALIAS] = "wn",
    [VED_COM_WIN_CHANGE_PREV_FOCUSED] = "winprevfocused",
    [VED_COM_WIN_CHANGE_PREV_FOCUSED_ALIAS] = "w`",
    [VED_COM_WIN_CHANGE_PREV] = "winprev",
    [VED_COM_WIN_CHANGE_PREV_ALIAS] = "wp",
    [VED_COM_WRITE_FORCE] = "write!",
    [VED_COM_WRITE_FORCE_ALIAS] = "w!",
    [VED_COM_WRITE] = "write",
    [VED_COM_WRITE_ALIAS] = "w",
    [VED_COM_WRITE_QUIT_FORCE] = "wq!",
    [VED_COM_WRITE_QUIT] = "wq",
    [VED_COM_END] = NULL
  };

  int num_args[VED_COM_END + 1] = {
    [VED_COM_BUF_DELETE_FORCE ... VED_COM_BUF_DELETE_ALIAS] = 1,
    [VED_COM_BUF_CHANGE ... VED_COM_BUF_CHANGE_ALIAS] = 1,
    [VED_COM_SUBSTITUTE ... VED_COM_SUBSTITUTE_ALIAS] = 5,
    [VED_COM_WRITE_FORCE ... VED_COM_WRITE_FORCE_ALIAS] = 3,
    [VED_COM_WRITE ... VED_COM_WRITE_ALIAS] = 3,
  };

  int flags[VED_COM_END + 1] = {
    [VED_COM_BUF_DELETE_FORCE ... VED_COM_BUF_DELETE_ALIAS] = RL_VED_ARG_BUFNAME,
    [VED_COM_BUF_CHANGE ... VED_COM_BUF_CHANGE_ALIAS] = RL_VED_ARG_BUFNAME,
    [VED_COM_EDIT_FORCE ... VED_COM_ENEW] = RL_VED_ARG_FILENAME,
    [VED_COM_READ ... VED_COM_READ_ALIAS] = RL_VED_ARG_FILENAME,
    [VED_COM_SUBSTITUTE ... VED_COM_SUBSTITUTE_ALIAS] =
      RL_VED_ARG_RANGE|RL_VED_ARG_GLOBAL|RL_VED_ARG_PATTERN|RL_VED_ARG_SUB|RL_VED_ARG_INTERACTIVE,
    [VED_COM_WRITE_FORCE ... VED_COM_WRITE_ALIAS] =
      RL_VED_ARG_FILENAME|RL_VED_ARG_RANGE|RL_VED_ARG_BUFNAME|RL_VED_ARG_APPEND,
  };

#define add_arg(arg)\
  VED_COMMANDS[i]->args[j] = Alloc (bytelen (arg) + 1); \
  strcpy (VED_COMMANDS[i]->args[j++], arg)

  VED_COMMANDS = Alloc (sizeof (rlcom_t) * (VED_COM_END + 1));

  int i = 0;
  for (i = 0; i < VED_COM_END; i++) {
    VED_COMMANDS[i] = AllocType (rlcom);
    VED_COMMANDS[i]->com = Alloc (bytelen (ved_commands[i]) + 1);
    strcpy (VED_COMMANDS[i]->com, ved_commands[i]);

    ifnot (num_args[i]) {
      VED_COMMANDS[i]->args = NULL;
      continue;
    }

    VED_COMMANDS[i]->args = Alloc (sizeof (char *) * (num_args[i] + 1));

    int j = 0;
    if (flags[i] & RL_VED_ARG_BUFNAME) { add_arg ("--bufname=");}
    if (flags[i] & RL_VED_ARG_RANGE) { add_arg ("--range="); }
    if (flags[i] & RL_VED_ARG_INTERACTIVE) { add_arg ("--interactive"); }
    if (flags[i] & RL_VED_ARG_GLOBAL) { add_arg ("--global"); }
    if (flags[i] & RL_VED_ARG_SUB) { add_arg ("--sub="); }
    if (flags[i] & RL_VED_ARG_PATTERN) { add_arg ("--pat="); }
    if (flags[i] & RL_VED_ARG_APPEND) { add_arg ("--append"); }
    VED_COMMANDS[i]->args[j] = NULL;
  }

  VED_COMMANDS[i] = NULL;
#undef add_arg
}

private rline_t *rline_new (ed_t *ed, term_t *this, utf8 (*getch) (term_t *), int prompt_row,
int num_cols, video_t *video) {
  rline_t *rl = AllocType (rline);
  rl->prompt_char = DEFAULT_PROMPT_CHAR;
  rl->prompt_row = prompt_row;
  rl->num_cols = num_cols;
  rl->first_row = prompt_row;
  rl->row_pos = rl->prompt_row;
  rl->fd = $my(out_fd);
  rl->line = AllocType (vstr);
  vstring_t *s = AllocType (vstring);
  s->data = string_new_with (" ");
  current_list_append (rl->line, s);

  rl->ed = ed;
  rl->term = this;
  rl->cur_video = video;
  rl->getch = getch;
  rl->at_beg = rline_call_at_beg;
  rl->at_end = rline_call_at_end;
  rl->tab_completion = rline_tab_completion;

  rl->state |= (RL_OK|RL_IS_VISIBLE);
  return rl;
}

private rline_t *ved_rline_new (ed_t *ed, term_t *this, utf8 (*getch) (term_t *), int prompt_row,
int num_cols, video_t *video) {
  rline_t *rl = rline_new (ed, this, getch , prompt_row, num_cols, video) ;
  if (VED_COMMANDS is NULL) ved_init_commands ();
  rl->commands = VED_COMMANDS;
  rl->tab_completion = ved_rline_tab_completion;
  return rl;
}

private void rline_write (rline_t *rl) {
  rl->state &= ~RL_WRITE;
  int orig_first_row = rl->first_row;

  if (rl->line->num_items + 1 <= rl->num_cols) {
    rl->num_rows = 1;
    rl->first_row = rl->prompt_row;
  } else {
    int mod = rl->line->num_items % rl->num_cols;
    rl->num_rows = (rl->line->num_items / rl->num_cols) + (mod isnot 0);
    if  (rl->num_rows is 0) rl->num_rows = 1;
    rl->first_row = rl->prompt_row - rl->num_rows + 1;
  }

  while (rl->first_row > orig_first_row)
    video_draw_at (rl->cur_video, orig_first_row++);

  string_t *render = string_new_with_fmt ("%s" TERM_GOTO_PTR_POS_FMT
      TERM_SET_COLOR_FMT "%c",
      TERM_CURSOR_HIDE, rl->first_row, 1, COLOR_PROMPT, rl->prompt_char);

  rl->row_pos = rl->prompt_row - rl->num_rows + 1;

  vstring_t *chars = rl->line->head;

  int cidx = 0;
  for (int i = 0; i < rl->num_rows; i++) {
    for (cidx = 0; cidx + (i * rl->num_cols) < rl->line->num_items and cidx < rl->num_cols; cidx++) {
      string_append (render, chars->data->bytes);
      chars = chars->next;
    }
  }

  while (cidx++ < rl->num_cols - 1) string_append_byte (render, ' ');

  int row = rl->first_row + (rl->line->cur_idx / (rl->line->num_items + 1));
  int col = 1 + (rl->prompt_char isnot 0) + rl->line->cur_idx;
  int i;
  for (i = 0; i + 1 < rl->num_rows and rl->line->cur_idx > ((i + 1) * (rl->num_cols)) - 1
     - (rl->prompt_char isnot 0); i++) col -= rl->num_cols;
  row = rl->first_row + i;

  string_append_fmt (render, "%s%s", TERM_COLOR_RESET,
    (rl->state & RL_CURSOR_HIDE) ? "" : TERM_CURSOR_SHOW);
  string_append_fmt (render, TERM_GOTO_PTR_POS_FMT, row, col);
  fd_write (rl->fd, render->bytes, render->num_bytes);
  string_free (render);
}

private rline_t *rline_edit (rline_t *rl) {
  ed_t *this = rl->ed;
  vstring_t *ch;
  int retval;

  if (rl->state & RL_CLEAR) {
    rline_clear (rl);
    if (rl->state & RL_BREAK) goto theend;
  }

  if (rl->state & RL_WRITE) {
    if (rl->state & RL_IS_VISIBLE)
      rline_write (rl);
    else
      rl->state &= ~RL_WRITE;

    if (rl->state & RL_BREAK) goto theend;
  }

  if (rl->state & RL_PROCESS_CHAR) goto process_char;
  if (rl->state & RL_INSERT_CHAR) goto insert_char;

  for (;;) {
thecontinue:
    rl->state &= ~RL_CONTINUE;
    if (rl->state & RL_IS_VISIBLE) rline_write (rl);
    rl->c = rl->getch (rl->term);
    retval = rl->at_beg (&rl);
    switch (retval) {
      case RL_BREAK: goto theend;
      case RL_PROCESS_CHAR: goto process_char;
      case RL_CONTINUE: goto thecontinue;
      case RL_POST_PROCESS: goto post_process;
    }

process_char:
    rl->state &= ~RL_PROCESS_CHAR;

    switch (rl->c) {
      case ESCAPE_KEY:
      case '\r':
        goto theend;

      case ARROW_UP_KEY:
        rl = rline_complete_history (rl, &$my(history)->rline->history_idx, 1);
        goto post_process;

      case ARROW_DOWN_KEY:
        rl = rline_complete_history (rl, &$my(history)->rline->history_idx, -1);
        goto post_process;

      case ARROW_LEFT_KEY:
         if (rl->line->cur_idx > 0) {
           rl->line->current = rl->line->current->prev;
           rl->line->cur_idx--;
         }
         goto post_process;

      case ARROW_RIGHT_KEY:
         if (rl->line->cur_idx < (rl->line->num_items - 1)) {
           rl->line->current = rl->line->current->next;
           rl->line->cur_idx++;
         }
         goto post_process;

      case HOME_KEY:
      case CTRL('a'):
        rl->line->cur_idx = 0;
        rl->line->current = rl->line->head;
        goto post_process;

      case END_KEY:
      case CTRL('e'):
        rl->line->cur_idx = (rl->line->num_items - 1);
        rl->line->current =  rl->line->tail;
        goto post_process;

      case DELETE_KEY:
        if (rl->line->cur_idx is (rl->line->num_items - 1) or
           (rl->line->cur_idx is 0 and rl->line->current->data->bytes[0] is ' ' and
            rl->line->num_items is 0))
          goto post_process;
        {
          vstring_t *tmp = current_list_pop (rl->line, vstring_t);
          if (NULL isnot tmp) {string_free (tmp->data); free (tmp);}
        }
        goto post_process;

      case BACKSPACE_KEY: {
          if (rl->line->cur_idx is 0) continue;
          rl->line->current = rl->line->current->prev;
          rl->line->cur_idx--;
          vstring_t *tmp = current_list_pop (rl->line, vstring_t);
          if (NULL isnot tmp) {string_free (tmp->data); free (tmp);}
        }

        goto post_process;

      case '\t':
        retval = rl->tab_completion (rl);
        switch (retval) {
          case RL_PROCESS_CHAR: goto process_char;
        }
        goto post_process;

      default:
insert_char:
        rl->state &= ~RL_INSERT_CHAR;

        if (rl->c < ' ' or
            rl->c is INSERT_KEY or
           (rl->c > 0x7f and (rl->c < 0x0a0 or (rl->c >= FN_KEY(1) and rl->c <= FN_KEY(12)) or
           (rl->c >= ARROW_DOWN_KEY and rl->c < HOME_KEY) or
           (rl->c > HOME_KEY and (rl->c is PAGE_DOWN_KEY or rl->c is PAGE_UP_KEY or rl->c is END_KEY))
           )))
          goto post_process;

        if (rl->c < 0x80) {
          ch = AllocType (vstring);
          ch->data = string_new_with_fmt ("%c", rl->c);
        } else {
          ch = AllocType (vstring);
          char buf[5];
          ch->data = string_new_with (char_from_code (rl->c, buf));
        }

        if (rl->line->cur_idx is rl->line->num_items - 1 and ' ' is rl->line->current->data->bytes[0]) {
          current_list_prepend (rl->line, ch);
          rl->line->current = rl->line->current->next;
          rl->line->cur_idx++;
        } else if (rl->line->cur_idx isnot rl->line->num_items - 1) {
          current_list_prepend (rl->line, ch);
          rl->line->current = rl->line->current->next;
          rl->line->cur_idx++;
        }
        else
          current_list_append (rl->line, ch);
        goto post_process;
    }

post_process:
    rl->state &= ~RL_POST_PROCESS;
    if (rl->state & RL_BREAK) goto theend;
    retval = rl->at_end (&rl);
    switch (retval) {
      case RL_BREAK: goto theend;
      case RL_PROCESS_CHAR: goto process_char;
    }
  }

theend:;
  rl->state &= ~RL_BREAK;
  return rl;
}

private rline_t *ved_rline_parse (buf_t *this, rline_t *rl) {
  vstring_t *it = ved_rline_get_command (rl);

  while (it) {
    int failed = 0;
    int type = 0;
    if (str_eq (it->data->bytes, " ")) goto itnext;

    arg_t *arg = AllocType (arg);
    string_t *opt = NULL;

    if (it->data->bytes[0] is '-') {
      if (it->next and it->next->data->bytes[0] is '-') {
        type |= RL_TOK_ARG_LONG;
        it = it->next;
      } else
        type |= RL_TOK_ARG_SHORT;

      it = it->next;
      if (it is NULL) {failed = 1; goto itnext;}

      opt = My(String).new ();
      while (it) {
        if (it->data->bytes[0] is ' ')
          goto arg_type;

        if (it->data->bytes[0] is '=') {
          if (it->next is NULL) {failed = 1; goto itnext;}

          it = it->next;

          arg->val = My(String).new ();
          int is_quoted = '"' is it->data->bytes[0];
          if (is_quoted) it = it->next;

          while (it) {
            if (' ' is it->data->bytes[0])
              ifnot (is_quoted) {
                type |= RL_TOK_ARG_OPTION;
                goto arg_type;
              }

            if ('"' is it->data->bytes[0])
              if (is_quoted) {
                if (arg->val->bytes[arg->val->num_bytes - 1] is '\\') {
                  arg->val->bytes[arg->val->num_bytes - 1] = '"';
                  it = it->next;
                  continue;
                }
                else { /* accept empty string --opt="" */
                  type |= RL_TOK_ARG_OPTION;
                  goto arg_type;
                }
              }

            string_append (arg->val, it->data->bytes);
            it = it->next;
          }

          goto arg_type;
        }

        string_append (opt, it->data->bytes);
        it = it->next;
      }

arg_type:
      if (type & RL_TOK_ARG_OPTION) {
        if (str_eq (opt->bytes, "pat"))
          arg->type |= RL_VED_ARG_PATTERN;
        else if (str_eq (opt->bytes, "sub"))
          arg->type |= RL_VED_ARG_SUB;
        else if (str_eq (opt->bytes, "range"))
          arg->type |= RL_VED_ARG_RANGE;
        else if (str_eq (opt->bytes, "bufname"))
          arg->type |= RL_VED_ARG_BUFNAME;
        else
          goto argtype_failed;

        goto argtype_succeed;
      } else {
        if (str_eq (opt->bytes, "i") or str_eq (opt->bytes, "interactive"))
          arg->type |= RL_VED_ARG_INTERACTIVE;
        else if (str_eq (opt->bytes, "global"))
          arg->type |= RL_VED_ARG_GLOBAL;
        else if (str_eq (opt->bytes, "append"))
          arg->type |= RL_VED_ARG_APPEND;
        else {
          arg->type |= RL_VED_ARG_ANYTYPE;
          arg->option = My(String).new_with (opt->bytes);
          }
        goto argtype_succeed;
      }

argtype_failed:
      My(String).free (opt);
      goto itnext;
argtype_succeed:
      My(String).free (opt);
      goto append_arg;
    } else {

      arg->option = My(String).new_with (it->data->bytes);
      it = it->next;
      while (it isnot NULL and 0 is (str_eq (it->data->bytes, " "))) {
        My(String).append (arg->option, it->data->bytes);
        it = it->next;
      }

      if (rl->com is VED_COM_BUF_CHANGE or rl->com is VED_COM_BUF_CHANGE_ALIAS) {
        opt = My(String).new_with ("bufname");
        arg->val = My(String).new_with (arg->option->bytes);
        type |= RL_TOK_ARG_OPTION;
        goto arg_type;
      }

      arg->type = RL_VED_ARG_FILENAME;
    }

append_arg:
    current_list_append (rl, arg);
itnext:
    if (failed) free (arg);
    it = it->next;
  }

  (void) this;
  return rl;
}

private arg_t *rline_get_arg (rline_t *rl, int type) {
  arg_t *arg = rl->tail;
  while (arg) {
    if (arg->type & type) return arg;
    arg = arg->prev;
  }

  return NULL;
}

private arg_t *rline_get_anytype_arg (rline_t *rl, char *argname) {
  arg_t *arg = rl->tail;
  while (arg) {
    if (arg->type & RL_VED_ARG_ANYTYPE) {
      if (str_eq (arg->option->bytes, argname)) return arg;
    }
    arg = arg->prev;
  }

  return NULL;
}

private int ved_rline_parse_range (buf_t *this, rline_t *rl, arg_t *arg) {
  if (arg is NULL) {
    rl->range[0] = rl->range[1] = this->cur_idx;
    return OK;
  }

  if (arg->val->num_bytes is 1) {
    if (arg->val->bytes[0] is '%') {
      rl->range[0] = 0; rl->range[1] = this->num_items - 1;
      return OK;
    }

    if (arg->val->bytes[0] is '.') {
      rl->range[0] = rl->range[1] = this->cur_idx;
      return OK;
    }

    if ('0' < arg->val->bytes[0] and arg->val->bytes[0] <= '9') {
      rl->range[0] = rl->range[1] = (arg->val->bytes[0] - '0') - 1;
      if (rl->range[0] >= this->num_items) return NOTOK;
      return OK;
    }

    return NOTOK;
  }

  char *sp = strstr (arg->val->bytes, ",");

  if (NULL is sp) {
    sp = arg->val->bytes;

    int num = 0;
    int idx = 0;
    while ('0' <= *sp and *sp <= '9' and idx++ <= MAX_COUNT_DIGITS)
      num = (10 * num) + (*sp++ - '0');

    if (*sp isnot 0) return NOTOK;
    rl->range[0] = rl->range[1] = num - 1;
    if (rl->range[0] >= this->num_items or rl->range[0] < 0) return NOTOK;
    return OK;
  }

  int diff = sp - arg->val->bytes;
  sp++;
  do {
    if (*sp is '.') {
      if (*(sp + 1) isnot 0) return NOTOK;
      rl->range[1] = this->cur_idx;
      break;
    }

    if (*sp is '$') {
      if (*(sp + 1) isnot 0) return NOTOK;
      rl->range[1] = this->num_items - 1;
      break;
    }

    if (*sp > '9' or *sp < '0') return NOTOK;
    int num = 0;
    int idx = 0;
    while ('0' <= *sp and *sp <= '9' and idx++ <= MAX_COUNT_DIGITS)
      num = (10 * num) + (*sp++ - '0');

    if (*sp isnot 0) return NOTOK;
    rl->range[1] = num - 1;
  } while (0);

  My(String).clear_at (arg->val, diff);
  ifnot (arg->val->num_bytes) return NOTOK;
  sp = arg->val->bytes;

  loop (1) {
    if (*sp is '.') {
      if (*(sp + 1) isnot 0) return NOTOK;
      rl->range[0] = this->cur_idx;
      break;
    }

    if (*sp > '9' or *sp < '0') return NOTOK;
    int num = 0;
    int idx = 0;
    while ('0' <= *sp and *sp <= '9' and idx++ <= MAX_COUNT_DIGITS)
      num = (10 * num) + (*sp++ - '0');

    if (*sp isnot 0) return NOTOK;
    rl->range[0] = num - 1;
  } while (0);

  if (rl->range[0] < 0) return NOTOK;
  if (rl->range[0] > rl->range[1]) return NOTOK;
  if (rl->range[1] >= this->num_items) return NOTOK;
  return OK;
}

private int ved_rline (buf_t **thisp, rline_t *rl) {
  int retval = NOTHING_TODO;

  buf_t *this = *thisp;
  int is_special_win = str_eq ($myparents(name), VED_SPECIAL_WIN);

  rl = rline_edit (rl);

  if (rl->c isnot '\r') goto theend;
  if (rl->line->head is NULL or rl->line->head->data->bytes[0] is ' ')
    goto theend;

  ved_rline_parse  (this, rl);

  switch (rl->com) {
    case VED_COM_WRITE_FORCE_ALIAS:
      rl->com = VED_COM_WRITE_FORCE; //__fallthrough__;
    case VED_COM_WRITE_FORCE:
    case VED_COM_WRITE:
    case VED_COM_WRITE_ALIAS:
#ifdef ENABLE_WRITING
      {
        arg_t *fname = rline_get_arg (rl, RL_VED_ARG_FILENAME);
        arg_t *range = rline_get_arg (rl, RL_VED_ARG_RANGE);
        arg_t *append = rline_get_arg (rl, RL_VED_ARG_APPEND);
        if (NULL is fname) {
          if (is_special_win) goto theend;
          if (NULL isnot range or NULL isnot append) goto theend;
          retval = ved_write_buffer (this, VED_COM_WRITE_FORCE is rl->com);
        } else {
          if (NULL is range) {
            rl->range[0] = 0;
            rl->range[1] = this->num_items - 1;
          } else
            if (NOTOK is ved_rline_parse_range (this, rl, range))
              goto theend;
          retval = ved_write_to_fname (this, fname->option->bytes, NULL isnot append,
            rl->range[0], rl->range[1], VED_COM_WRITE_FORCE is rl->com);
        }
      }
#else
  My(Msg).error ($my(root), "writing is disabled, to enable use DEBUG=1 during compilation");
#endif
      goto theend;

    case VED_COM_EDIT_FORCE_ALIAS:
      rl->com = VED_COM_EDIT_FORCE; //__fallthrough__;
    case VED_COM_EDIT_FORCE:
    case VED_COM_EDIT:
    case VED_COM_EDIT_ALIAS:
      if (is_special_win) goto theend;
      {
        arg_t *arg = rline_get_arg (rl, RL_VED_ARG_FILENAME);
        retval = ved_edit_fname (thisp, (NULL is arg ? NULL : arg->option->bytes),
           $myparents(cur_frame), VED_COM_EDIT_FORCE is rl->com, 1);
      }
      goto theend;

    case VED_COM_QUIT_FORCE_ALIAS:
      rl->com = VED_COM_QUIT_FORCE; //__fallthrough__;
    case VED_COM_QUIT_FORCE:
    case VED_COM_QUIT:
    case VED_COM_QUIT_ALIAS:
      if (is_special_win) goto theend;
      retval = ved_quit (this, VED_COM_QUIT_FORCE is rl->com);
      goto theend;

    case VED_COM_WRITE_QUIT:
    case VED_COM_WRITE_QUIT_FORCE:
      if (is_special_win) goto theend;
      ved_write_buffer (this, 0);
      retval = ved_quit (this, VED_COM_WRITE_QUIT_FORCE is rl->com);
      goto theend;

    case VED_COM_ENEW:
       {
         arg_t *arg = rline_get_arg (rl, RL_VED_ARG_FILENAME);
         if (NULL isnot arg)
           retval = ved_enew_fname (thisp, arg->option->bytes);
         else
           retval = ved_enew_fname (thisp, UNAMED);
        }
        goto theend;

    case VED_COM_MESSAGES:
      retval = ed_msg_buf (thisp);
      goto theend;

    case VED_COM_SPLIT:
      {
        if (is_special_win) goto theend;
        arg_t *arg = rline_get_arg (rl, RL_VED_ARG_FILENAME);
        if (NULL is arg)
          retval = ved_split (thisp, UNAMED);
        else
          retval = ved_split (thisp, arg->option->bytes);
      }
      goto theend;


    case VED_COM_READ:
    case VED_COM_READ_ALIAS:
      {
        arg_t *arg = rline_get_arg (rl, RL_VED_ARG_FILENAME);
        if (NULL is arg) goto theend;
        retval = ved_buf_read (this, arg->option->bytes);
        goto theend;
      }

    case VED_COM_SUBSTITUTE:
    case VED_COM_SUBSTITUTE_WHOLE_FILE_AS_RANGE:
    case VED_COM_SUBSTITUTE_ALIAS:
      {
        arg_t *pat = rline_get_arg (rl, RL_VED_ARG_PATTERN);
        arg_t *sub = rline_get_arg (rl, RL_VED_ARG_SUB);
        arg_t *global = rline_get_arg (rl, RL_VED_ARG_GLOBAL);
        arg_t *interactive = rline_get_arg (rl, RL_VED_ARG_INTERACTIVE);
        arg_t *range = rline_get_arg (rl, RL_VED_ARG_RANGE);

        if (pat is NULL or sub is NULL) goto theend;

        if (NULL is range and rl->com is VED_COM_SUBSTITUTE_WHOLE_FILE_AS_RANGE) {
          rl->range[0] = 0; rl->range[1] = this->num_items - 1;
        } else
          if (NOTOK is ved_rline_parse_range (this, rl, range))
            goto theend;

        retval = ved_substitute (this, pat->val->bytes, sub->val->bytes,
           global isnot NULL, interactive isnot NULL, rl->range[0], rl->range[1]);
       }

       goto theend;

    case VED_COM_BUF_CHANGE_PREV_ALIAS:
      rl->com = VED_COM_BUF_CHANGE_PREV; //__fallthrough__;
    case VED_COM_BUF_CHANGE_PREV:
      if (is_special_win) goto theend;
      retval = ved_buf_change (thisp, rl->com);
      goto theend;

    case VED_COM_BUF_CHANGE_NEXT_ALIAS:
      rl->com = VED_COM_BUF_CHANGE_NEXT; //__fallthrough__;
    case VED_COM_BUF_CHANGE_NEXT:
      if (is_special_win) goto theend;
      retval = ved_buf_change (thisp, rl->com);
      goto theend;

    case VED_COM_BUF_CHANGE_PREV_FOCUSED_ALIAS:
      rl->com = VED_COM_BUF_CHANGE_PREV_FOCUSED; //__fallthrough__;
    case VED_COM_BUF_CHANGE_PREV_FOCUSED:
      if (is_special_win) goto theend;
      retval = ved_buf_change (thisp, rl->com);
      goto theend;

    case VED_COM_BUF_CHANGE_ALIAS:
      rl->com = VED_COM_BUF_CHANGE; //__fallthrough__;
    case VED_COM_BUF_CHANGE:
      if (is_special_win) goto theend;
      {
        arg_t *bufname = rline_get_arg (rl, RL_VED_ARG_BUFNAME);
        if (NULL is bufname) goto theend;
        retval = ved_buf_change_bufname (thisp, bufname->val->bytes);
      }
      goto theend;

    case VED_COM_WIN_CHANGE_PREV_ALIAS:
      rl->com = VED_COM_WIN_CHANGE_PREV; //__fallthrough__;
    case VED_COM_WIN_CHANGE_PREV:
      retval = ved_win_change (thisp, rl->com, NULL, 0);
      goto theend;

    case VED_COM_WIN_CHANGE_NEXT_ALIAS:
      rl->com = VED_COM_WIN_CHANGE_NEXT; //__fallthrough__;
    case VED_COM_WIN_CHANGE_NEXT:
      retval = ved_win_change (thisp, rl->com, NULL, 0);
      goto theend;

    case VED_COM_WIN_CHANGE_PREV_FOCUSED_ALIAS:
      rl->com = VED_COM_WIN_CHANGE_PREV_FOCUSED; //__fallthrough__;
    case VED_COM_WIN_CHANGE_PREV_FOCUSED:
      retval = ved_win_change (thisp, rl->com, NULL, 0);
      goto theend;

    case VED_COM_BUF_DELETE_FORCE_ALIAS:
      rl->com = VED_COM_BUF_DELETE_FORCE; //__fallthrough__;
    case VED_COM_BUF_DELETE_FORCE:
    case VED_COM_BUF_DELETE_ALIAS:
    case VED_COM_BUF_DELETE:
      if (is_special_win) goto theend;
      retval = ved_buf_delete (thisp, $my(parent)->cur_idx,
        rl->com is VED_COM_BUF_DELETE_FORCE);
      goto theend;

    default: goto theend;
  }

theend:
  if (DONE is retval) {
    rl->state &= ~RL_CLEAR_FREE_LINE;
    rline_clear (rl);
    rline_history_push (rl);
  }
  else {
    rline_clear (rl);
    rline_free (rl);
  }

  return retval;
}

private int ved_insert (buf_t *this, utf8 com) {
  utf8 c = 0;
  if (com is '\n') {
    com = 'i';
    c = '\n';
  }

  string_t *cur_insert = string_new ();

  char buf[16];
  char prev_mode[bytelen ($my(mode)) + 1];

  memcpy (prev_mode, $my(mode), sizeof (prev_mode));
  strcpy ($my(mode), INSERT_MODE);

  action_t *action = AllocType (action);
  act_t *act = AllocType (act);
  vundo_set (act, REPLACE_LINE);
  act->idx = this->cur_idx;
  act->num_bytes = $mycur(data)->num_bytes;
  act->bytes = str_dup ($mycur(data)->bytes, $mycur(data)->num_bytes);

  action = stack_push (action, act);

  if (com is 'A' or $mycur(cur_col_idx) is (int) $mycur(data)->num_bytes -
      char_byte_len ($mycur(data)->bytes[$mycur(cur_col_idx)])) {
    ADD_TRAILING_NEW_LINE;

    ved_normal_eol (this);
  } else if (com is 'a')
    ved_normal_right (this, 1, 1);

  if (c isnot 0) goto handle_char;

  buf_set_draw_topline (this);

theloop:
  for (;;) {

get_char:
    ed_check_msg_status (this);
    c = My(Input).get ($my(term_ptr));

handle_char:
    if (c > 0x7f)
      if (c < 0x0a0 or (c >= FN_KEY(1) and c <= FN_KEY(12)) or c is INSERT_KEY)
        continue;

    if ((c is '\t') or (c >= ' ' and c <= '~') or
       (c > 0x7f and ((c < ARROW_DOWN_KEY) or (c > HOME_KEY and
       (c isnot DELETE_KEY) and (c isnot PAGE_DOWN_KEY) and (c isnot PAGE_UP_KEY)
        and (c isnot END_KEY)))))
      goto new_char;

    switch (c) {
      case ESCAPE_KEY:
        goto theend;

      case CTRL('n'):
        ved_complete_word (this);
        goto get_char;

      case CTRL('x'):
        ved_handle_ctrl_x (this);
        goto get_char;

      case CTRL('a'):
        ved_insert_last_insert (this);
        goto get_char;

      case CTRL('v'):
        if (NEWCHAR is ved_insert_character (this, &c))
          goto new_char;
        goto get_char;

      case CTRL('k'):
        if (NEWCHAR is ved_complete_digraph (this, &c))
          goto new_char;
        goto get_char;

      case CTRL('y'):
      case CTRL('e'):
        if (NEWCHAR is ved_insert_handle_ud_line_completion (this, &c))
          goto new_char;
        goto get_char;

      case BACKSPACE_KEY:
        ifnot ($mycur(cur_col_idx)) goto get_char;

        ved_normal_left (this, 1);
        ved_normal_delete (this, 1, -1);
        goto get_char;

      case DELETE_KEY:
        if ($mycur(data)->num_bytes is 0 or
          // HAS_THIS_LINE_A_TRAILING_NEW_LINE or
            $mycur(data)->bytes[$mycur(cur_col_idx)] is 0 or
            $mycur(data)->bytes[$mycur(cur_col_idx)] is '\n') {
          if (HAS_THIS_LINE_A_TRAILING_NEW_LINE) {
            if ($mycur(cur_col_idx) + 1 is (int) $mycur(data)->num_bytes)
              ved_normal_left (this, 1);
            RM_TRAILING_NEW_LINE;
          }

          if (DONE is ved_join (this)) {
            act_t *lact = stack_pop (action, act_t);
            if (lact isnot NULL) {
              free (lact->bytes);
              free (lact);
            }
          }

          if ($mycur(cur_col_idx) is (int) $mycur(data)->num_bytes - 1)
            ADD_TRAILING_NEW_LINE;

          if ($mycur(cur_col_idx) isnot 0)
            ved_normal_right (this, 1, 1);

          goto get_char;
        }

        ved_normal_delete (this, 1, -1);
        goto get_char;

      case HOME_KEY:
        ved_normal_bol (this);
        goto get_char;

      case END_KEY:
        ADD_TRAILING_NEW_LINE;
        ved_normal_eol (this);
        goto get_char;

      case ARROW_RIGHT_KEY:
        if($mycur(cur_col_idx) is (int) $mycur(data)->num_bytes -
            char_byte_len ($mycur(data)->bytes[$mycur(cur_col_idx)]))
          ADD_TRAILING_NEW_LINE;
        ved_normal_right (this, 1, 1);
        goto get_char;

      case ARROW_LEFT_KEY:
        ved_normal_left (this, 1);
        goto get_char;

      case ARROW_UP_KEY:
      case ARROW_DOWN_KEY:
      case PAGE_UP_KEY:
      case PAGE_DOWN_KEY:
      case  '\r':
      case  '\n':
        insert_change_line (this, c, &action);

        if ('\r' is c and cur_insert->num_bytes) {
          string_replace_with ($my(last_insert), cur_insert->bytes);
          string_clear (cur_insert);
        }

        if ('\n' is c) goto theend;

        goto theloop;

      default:
        goto get_char;
    }

  int orig_col;
new_char:
    orig_col = $mycur(cur_col_idx)++;

    if ('~' >= c and c >= ' ') {
      buf[0] = c;
      buf[1] = '\0';
    }
/*    else {
      if (c <  ???? )
        $mycur(cur_col_idx) = orig_col;
        msg_error_fmt ("UnHandled char: |%c| int: %d", c, c);
        goto get_char;
      }
*/
    else if (c is '\t') {
      if ($my(ftype)->tab_indents is 0 or ($my(state) & ACCEPT_TAB_WHEN_INSERT)) {
        $my(state) &= ~ACCEPT_TAB_WHEN_INSERT;
        buf[0] = c;
        buf[1] = '\0';
      } else {
        $mycur(cur_col_idx) += $my(ftype)->shiftwidth - 1;
        $my(video)->col_pos = $my(cur_video_col) =
            $my(video)->col_pos + $my(ftype)->shiftwidth - 1;

        int i;
        for (i = 0; i < $my(ftype)->shiftwidth; i++) buf[i] = ' ';
        buf[i] = '\0';
      }
    }
    else {
      $my(state) &= ~ACCEPT_TAB_WHEN_INSERT;
      $mycur(cur_col_idx)++;
      if (c < 0x800) {
        buf[0] = (c >> 6) | 0xC0;
        buf[1] = (c & 0x3F) | 0x80;
        buf[2] = '\0';
      }
      else {
        $mycur(cur_col_idx)++;
        if (c < 0x10000) {
          buf[0] = (c >> 12) | 0xE0;
          buf[1] = ((c >> 6) & 0x3F) | 0x80;
          buf[2] = (c & 0x3F) | 0x80;
          buf[3] = '\0';
        }
        else if (c < 0x110000) {
          $mycur(cur_col_idx)++;
          buf[0] = (c >> 18) | 0xF0;
          buf[1] = ((c >> 12) & 0x3F) | 0x80;
          buf[2] = ((c >> 6) & 0x3F) | 0x80;
          buf[3] = (c & 0x3F) | 0x80;
          buf[4] = '\0';
        }
      }
    }

    My(String).insert_at ($mycur(data), buf, orig_col);
    string_append (cur_insert, buf);
    if ($my(video)->col_pos is $my(dim)->num_cols) {
      $mycur(first_col_idx) = $mycur(cur_col_idx);
      $my(video)->col_pos = $my(cur_video_col) = 1;
    }
    else
      $my(video)->col_pos = $my(cur_video_col) = $my(video)->col_pos + 1;

    $my(flags) |= BUF_IS_MODIFIED;
    self(draw_cur_row);

    goto get_char;
  }

theend:
  if ($mycur(data)->num_bytes)
    RM_TRAILING_NEW_LINE;

  ved_normal_left (this, 1);
  strcpy ($my(mode), prev_mode);
  buf_set_draw_topline (this);

  if (cur_insert->num_bytes)
    string_replace_with ($my(last_insert), cur_insert->bytes);

  string_free (cur_insert);

  if (NULL isnot action->head)
    vundo_push (this, action);
  else
    free (action);
  return DONE;
}

private int ved_normal_test3 (buf_t **cur) {
  (void) cur;
  return DONE;
}

private int ved_buf_exec_cmd_handler (buf_t **thisp, utf8 com, int *range, int reg) {
  int count = 1;

  buf_t *this = *thisp;

  if (range[0] <= 0) {
    if (range[0] is 0)
      return INDEX_ERROR;
  } else {
    if (range[0] > this->num_items)
      if (range[1] >= 0)
        return INDEX_ERROR;
    count = range[0];
  }

  switch (com) {
    case 'q':
      if ($my(flags) & BUF_IS_PAGER) return BUF_QUIT;
      return NOTHING_TODO;

    case ':':
      {
      rline_t *rl = ved_rline_new ($my(root), $my(term_ptr), My(Input).get, *$my(prompt_row_ptr),
        $my(dim)->num_cols, $my(video));
      return ved_rline (thisp, rl);
      }

      break;
    case '/':
    case '?':
    case '*':
    case '#':
    case 'n':
    case 'N':
      return ved_search (this, com);

    case 'm':
      return mark_set (this, -1);

    case '`':
      return mark_goto (this);

    case '~':
      return ved_normal_change_case (this);

    case CTRL('a'):
    case CTRL('x'):
       return ved_word_math (this, count, com);

    case '^':
       return ved_normal_noblnk (this);

    case '>':
    case '<':
       return ved_indent (this, count, com);

    case 'y':
      return ved_normal_yank (this, count, reg);

    case 'Y':
      return ved_normal_Yank (this, count, reg);

    case ' ':
      ifnot (SPACE_ON_NORMAL_IS_LIKE_INSERT_MODE)
        return NOTHING_TODO;

      (&$my(regs)[REG_RDONLY])->type = CHARWISE;
      (&$my(regs)[REG_RDONLY])->head->data->bytes[0] = ' ';
      (&$my(regs)[REG_RDONLY])->head->data->bytes[1] = '\0';
      (&$my(regs)[REG_RDONLY])->head->data->num_bytes = 1;
      (&$my(regs)[REG_RDONLY])->head->next = NULL;
      (&$my(regs)[REG_RDONLY])->head->prev = NULL;
      return ved_normal_put (this, REG_RDONLY, 'P');

    case 'p':
    case 'P':
      return ved_normal_put (this, reg, com);

    case 'd':
      return ved_cmd_delete (this, count, reg);

    case 'x':
    case DELETE_KEY:
      return ved_normal_delete (this, count, reg);

    case BACKSPACE_KEY:
      ifnot ($mycur(cur_col_idx)) {
        if (BACKSPACE_ON_FIRST_IDX_REMOVE_TRAILING_SPACES) {
          size_t len = $mycur(data)->num_bytes;
          for (int i = $mycur(data)->num_bytes - 1; i > 0; i--)
            if ($mycur(data)->bytes[i] is ' ')
              My(String).clear_at ($mycur(data), i);
            else
              break;

          if (len isnot $mycur(data)->num_bytes) {
            $my(flags) |= BUF_IS_MODIFIED;
            self(draw_cur_row);
          }
          return DONE;
        }

        return NOTHING_TODO;
      }

      ifnot (BACKSPACE_ON_NORMAL_IS_LIKE_INSERT_MODE)
        return NOTHING_TODO;

      //__fallthrough__;

    case 'X':
       if (DONE is ved_normal_left (this, 1))
         return ved_normal_delete (this, count, reg);
       return NOTHING_TODO;

    case 'J':
      return ved_join (this);

    case '$':
      return ved_normal_eol (this);

    case CTRL('w'):
      return ved_normal_handle_ctrl_w (thisp);

    case 'g':
      return ved_normal_handle_g (this, count);

    case 'G':
      return ved_normal_handle_G (this, count);

    case '0':
      return ved_normal_bol (this);

    case 'E':
    case 'e':
      return ved_normal_end_word (this, count,
        (SMALL_E_ON_NORMAL_GOES_INSERT_MODE is 1 and 'e' is com));

    case ARROW_RIGHT_KEY:
    case 'l':
      return ved_normal_right (this, 1, 1);

    case ARROW_LEFT_KEY:
    case 'h':
      return ved_normal_left (this, 1);

    case ARROW_UP_KEY:
    case 'k':
      return ved_normal_up (this, count, 1, 1);

    case ARROW_DOWN_KEY:
    case 'j':
      return ved_normal_down (this, count, 1, 1);

    case PAGE_DOWN_KEY:
    case CTRL('f'):
      return ved_normal_page_down (this, count);

    case PAGE_UP_KEY:
    case CTRL('b'):
      return ved_normal_page_up (this, count);

    case HOME_KEY:
      return ved_normal_bof (this);

    case END_KEY:
      return ved_normal_eof (this);

    case CTRL('v'):
      return ved_visual_bwise (this);

    case 'V':
      return ved_normal_visual_lw (this);

    case 'v':
      return ved_normal_visual_cw (this);

    case 'D':
      return ved_normal_delete_eol (this, reg);

    case 'r':
      return ved_normal_replace_char (this);

    case 'C':
       ved_normal_delete_eol (this, reg);
       return ved_insert (this, com);

    case 'o':
    case 'O':
      return ved_insert_new_line (this, com);

    case '\r':
      ifnot (CARRIAGE_RETURN_ON_NORMAL_IS_LIKE_INSERT_MODE)
        return NOTHING_TODO;
      com = '\n';
//      __fallthrough__;
    case 'i':
    case 'a':
    case 'A':
      return ved_insert (this, com);

    case CTRL('r'):
    case 'u':
      return vundo (this, com);

    case CTRL('j'):
      My(Ed).suspend ($my(root));
      return EXIT;

    default:
      return 0;
  }

  return DONE;
}

#define ADD_RANGE(n) range[IS_FIRST_RANGE_OK] = n
#define IS_FIRST_RANGE_OK range[0] != -1
#define IS_SECOND_RANGE_OK range[1] != -1
#define ARE_BOTH_RANGES_OK (IS_FIRST_RANGE_OK && IS_SECOND_RANGE_OK)

#define NORMAL_GET_NUMBER                 \
  c = ({                                  \
    int i = 0;                            \
    char intbuf[8];                       \
    intbuf[i++] = c;                      \
    utf8 cc = BUF_GET_NUMBER (intbuf, i); \
                                          \
    if (i is MAX_COUNT_DIGITS)            \
      goto new_state;                     \
                                          \
    intbuf[i] = '\0';                     \
    ADD_RANGE (atoi (intbuf));            \
    cc;                                   \
    })

private int ved_loop (ed_t *ed, buf_t *this) {
  int retval = NOTOK;
  int range[2];
  utf8 c;
  int cmd_retv;
  int reg = -1;

  for (;;) {
new_state:
    reg = -1;
    range[0] = range[1] = -1;

get_char:
    ed_check_msg_status (this);
    c = My(Input).get ($my(term_ptr));

handle_char:
   switch (c) {
     case 't':
     case CTRL('k'):
       ved_normal_test3 (&this);
       continue;

     case CTRL('l'):
       self(draw);
       continue;

     case NOTOK: goto theend;

     case '"':
       if (-1 isnot reg) goto exec_block;

       reg = My(Input).get ($my(term_ptr));
       {
         char *r = strchr (REGISTERS, reg);
         reg = (NULL is strchr (REGISTERS, reg)) ? -1 : r - REGISTERS;
       }

       goto get_char;

     case '0':
        if (0 is (IS_FIRST_RANGE_OK)) goto exec_block;
        //__fallthrough__;

      case '1'...'9':
        if (ARE_BOTH_RANGES_OK) goto exec_block;
        NORMAL_GET_NUMBER;
        goto handle_char;

      case '.':
        if (ARE_BOTH_RANGES_OK) goto exec_block;
        ADD_RANGE (this->cur_idx + 1);
        goto get_char;

      case '$':
        if (0 is (IS_FIRST_RANGE_OK)) goto exec_block;
        if (ARE_BOTH_RANGES_OK) goto exec_block;
        ADD_RANGE (this->num_items);
        goto get_char;

      case ',':
        if (ARE_BOTH_RANGES_OK) goto exec_block;
        ifnot (IS_FIRST_RANGE_OK) range[0] = this->cur_idx + 1;
          goto get_char;

      case '%':
        if (ARE_BOTH_RANGES_OK) goto exec_block;
        if (IS_FIRST_RANGE_OK) goto exec_block;
        range[0] = 1; range[1] = this->num_items;
        goto get_char;

      default:
exec_block:
        if (reg is -1) reg = REG_UNAMED;

        cmd_retv = My(Ed).exec.cmd (&this, c, range, reg);

        if (cmd_retv is DONE || cmd_retv is NOTHING_TODO)
          goto new_state;

        if (cmd_retv is BUF_QUIT) {
          retval = ved_buf_change (&this, VED_COM_BUF_CHANGE_PREV_FOCUSED);
          if (retval is NOTHING_TODO) {
            retval = ved_win_change (&this, VED_COM_WIN_CHANGE_PREV_FOCUSED, NULL, 0);
            if (retval is NOTHING_TODO)
              cmd_retv = EXIT;
            else
              goto new_state;
          } else
            goto new_state;
        }

        if (cmd_retv is EXIT) {retval = OK; goto theend;}

        if (cmd_retv is WIN_EXIT) {
          retval = ved_win_delete (ed, &this);
          if (retval is DONE) goto new_state;
          if (retval is EXIT) {retval = OK; goto theend;}
          goto theend;
        }
    }
  }

theend:
  return retval;
}

private int ved_main (ed_t *this, buf_t *buf) {
  if ($my(state) & ED_SUSPENDED) {
    self(resume);
  } else
    My(Win).draw (this->current);

  My(Msg).send(this, COLOR_NORMAL, " ");
/*
  My(Msg).send (this, COLOR_CYAN,
      "Υγειά σου Κόσμε και καλό ταξίδι στο ραντεβού με την αιωνοιότητα");
 */

  return ved_loop (this, buf);
}

private buf_t *ed_get_current_buf (ed_t *this) {
  return this->current->current;
}

private win_t *ed_get_current_win (ed_t *this) {
  return this->current;
}

private int ed_get_current_win_idx (ed_t *this) {
  return this->cur_idx;
}

private win_t *ed_get_win_by_idx (ed_t *this, int idx) {
  if (idx >= this->num_items) return NULL;
  int i = 0;
  win_t *it = this->head;
  while (it) {
    if (i++ is idx) return it;
    it = it->next;
  }

  return NULL;
}

private int ed_get_state (ed_t *this) {
  return $my(state);
}

private win_t *ed_get_win_head (ed_t *this) {
  return this->head;
}

private win_t *ed_get_win_next (ed_t *this, win_t *w) {
  (void) this;
  return w->next;
}

private ed_t *ed_get_next (ed_t *this) {
  return this->next;
}

private ed_t *ed_get_prev (ed_t *this) {
  return this->prev;
}

private buf_t *ed_get_bufname (ed_t *this, char *fname) {
  buf_t *buf = NULL;
  win_t *w = this->head;
  int idx;
  while (w) {
    buf = My(Win).get.buf_by_fname (w, fname, &idx);
    if (buf) return buf;
    w = w->next;
  }

  return buf;
}

private win_t *ed_set_current_win (ed_t *this, int idx) {
  int cur_idx =this->cur_idx;
  if (INDEX_ERROR isnot current_list_set (this, idx))
    this->prev_idx = cur_idx;
  return this->current;
}

private void ed_set_screen_size (ed_t *this) {
  My(Term).restore ($my(term));
  My(Term).get_size ($my(term), &$my(term)->prop->lines, &$my(term)->prop->columns);
  My(Term).raw ($my(term));
  ifnot (NULL is $my(dim)) {
    free ($my(dim));
    $my(dim) = NULL;
  }

  $my(dim) = dim_new (1, $my(term)->prop->lines, 1, $my(term)->prop->columns);
  $my(msg_row) = $my(term)->prop->lines;
  $my(prompt_row) = $my(msg_row) - 1;
}

private dim_t *ed_set_dim (ed_t *this, int f_row, int l_row, int f_col, int l_col) {
  return dim_set ($my(dim), f_row, l_row, f_col, l_col);
}

private int ed_append_win (ed_t *this, win_t *w) {
  current_list_append (this, w);
  return this->cur_idx;
}

private void ed_suspend (ed_t *this) {
  if ($my(state) & ED_SUSPENDED) return;
  $my(state) |= ED_SUSPENDED;
  My(Screen).clear ($my(term));
  My(Term).restore ($my(term));
}

private void ed_resume (ed_t *this) {
  ifnot ($my(state) & ED_SUSPENDED) return;
  $my(state) &= ~ED_SUSPENDED;
  My(Term).raw ($my(term));
  My(Win).set.current_buf (this->current, this->current->cur_idx);
  My(Win).draw (this->current);
}

private void ed_free_reg (ed_t *this, rg_t *rg) {
  reg_t *reg = rg->head;
  while (reg isnot NULL) {
    reg_t *tmp = reg->next;
    My(String).free (reg->data);
    free (reg);
    reg = tmp;
  }

  rg->head = NULL;
}

private void ed_free (ed_t *this) {
  if (this is NULL) return;

  win_t *w = this->head;
  win_t *next;

  while (w isnot NULL) {
    next = w->next;
    win_free (w);
    w = next;
  }

  if ($myprop isnot NULL) {
    free ($my(dim));
    free ($my(saved_cwd));

    My(Term).free ($my(term));
    My(String).free ($my(topline));
    My(String).free ($my(msgline));
    My(String).free ($my(last_insert));
    My(String).free ($my(shared_str));
    My(Video).free ($my(video));

    history_free (&$my(history));
    env_free (&$my(env));

    for (int i = 0; i < NUM_REGISTERS; i++)
      ed_free_reg (this, &$my(regs)[i]);

    free ($myprop);
  }

  ved_deinit_commands ();

  free (this);
}

private ed_t *__ed_new__ (ed_T *E) {
  ed_t *this = AllocType (ed);
  this->prop = AllocProp (ed);

  $my(dim) = dim_new (1, E->prop->term->prop->lines, 1, E->prop->term->prop->columns);
  $my(has_topline) = $my(has_msgline) = $my(has_promptline) = 1;

  $my(Me) = E;
  $my(Cstring) = &E->Cstring;
  $my(String) = &E->String;
  $my(Re) = &E->Re;
  $my(Term) = &E->Term;
  $my(Input) = &E->Input;
  $my(Screen) = &E->Screen;
  $my(Cursor) = &E->Cursor;
  $my(Video) = &E->Video;
  $my(Win) = &E->Win;
  $my(Buf) = &E->Buf;
  $my(Msg) = &E->Msg;
  $my(Error) = &E->Error;

  $my(term) = My(Term).new ();
  $my(term)->prop->Me = &E->Term;
  My(Term).get_size ($my(term), &$my(term)->prop->lines, &$my(term)->prop->columns);
  $my(video) = My(Video).new (OUTPUT_FD, $my(term)->prop->lines, $my(term)->prop->columns, 1, 1);
  My(Term).raw ($my(term));

  $my(topline) = My(String).new ();
  $my(msgline) = My(String).new ();

  $my(msg_row) = $my(term)->prop->lines;
  $my(prompt_row) = $my(msg_row) - 1;

  for (int i = 0; i < NUM_REGISTERS; i++)
    $my(regs)[i] = (rg_t) {.reg = REGISTERS[i]};

  $my(regs)[REG_RDONLY].head = AllocType (reg);
  $my(regs)[REG_RDONLY].head->data = My(String).new_with ("     ");
  $my(regs)[REG_RDONLY].head->next = NULL;
  $my(regs)[REG_RDONLY].head->prev = NULL;

  $my(env) = env_new ();

  $my(history) = AllocType (hist);
  $my(history)->search = AllocType (h_search);
  $my(history)->rline = AllocType (h_rline);
  $my(history)->rline->history_idx = 0;
  $my(max_num_hist_entries) = RLINE_HISTORY_NUM_ENTRIES;
  $my(max_num_undo_entries) = UNDO_NUM_ENTRIES;

  $my(last_insert) = My(String).new ();
  $my(shared_str) = My(String).new ();

  $my(saved_cwd) = dir_get_current ();

  ved_init_commands ();

  return this;
}

private ed_t *ed_new (ed_T *E, int num_wins) {
  ed_t *this = __ed_new__ (E);
  if (num_wins <= 0) num_wins = 1;

  int num_frames = 1;

  win_t *w = self(win.new, VED_SPECIAL_WIN, 1);

  self(append.win, w);
  My(Win).buf_new_special (w, VED_MSG_BUF);

  loop (num_wins) {
    w = self(win.new, NULL, num_frames);
    self(append.win, w);
  }

  current_list_append (E, this);
  return this;
}

private ed_T *__allocate_prop__ (ed_T *this) {
  $myprop = AllocProp (ed);
  $my(Cstring) = &this->Cstring;
  $my(String) = &this->String;
  $my(Re) = &this->Re;
  $my(Term) = &this->Term;
  $my(Input) = &this->Input;
  $my(Screen) = &this->Screen;
  $my(Cursor) = &this->Cursor;
  $my(Video) = &this->Video;
  $my(Win) = &this->Win;
  $my(Buf) = &this->Buf;
  $my(Msg) = &this->Msg;
  $my(Error) = &this->Error;
  $my(Me) = this;
  return this;
}

private void __deallocate_prop__ (ed_T *this) {
  if ($myprop isnot NULL) {
    My(Video).free ($my(video));
    My(Term).free ($my(term));
    free ($myprop); $myprop = NULL;
  }
}

private int __init__ (ed_T *this) {
  __allocate_prop__ (this);

  $my(term) = My(Term).new ();
  $my(term)->prop->Me = &this->Term;

  if (My(Term).raw ($my(term)) is NOTOK) return NOTOK;

  setbuf (stdin, NULL);

  My(Cursor).get_pos  ($my(term),
      &$my(term)->prop->orig_curs_row_pos,
      &$my(term)->prop->orig_curs_col_pos);
  My(Term).get_size ($my(term), &$my(term)->prop->lines, &$my(term)->prop->columns);
  My(Screen).save ($my(term));
  My(Term).sane ($my(term));
  return OK;
}

private void __deinit__ (ed_T *this) {
  My(Term).sane ($my(term));
  My(Cursor).set_pos ($my(term), $my(term)->prop->orig_curs_row_pos,
                      $my(term)->prop->orig_curs_col_pos);
  My(Screen).restore ($my(term));
  __deallocate_prop__ (this);
}

public cstring_T __init_cstring__ (void) {
  return ClassInit (cstring,
    .self = SelfInit (cstring,
      .cmp_n = str_cmp_n,
      .dup = str_dup
    )
  );
}

public string_T __init_string__ (void) {
  return ClassInit (string,
    .self = SelfInit (string,
      .free = string_free,
      .new = string_new,
      .new_with = string_new_with,
      .new_with_fmt = string_new_with_fmt,
      .insert_at  = string_insert_at,
      .append = string_append,
      .append_byte = string_append_byte,
      .prepend = string_prepend,
      .prepend_fmt = string_prepend_fmt,
      .append_fmt = string_append_fmt,
      .delete_numbytes_at = string_delete_numbytes_at,
      .replace_numbytes_at_with = string_replace_numbytes_at_with,
      .replace_with = string_replace_with,
      .replace_with_fmt = string_replace_with_fmt,
      .clear = string_clear,
      .clear_at = string_set_nullbyte_at,
    )
  );
}

public void __deinit_string__ (string_T *this) {
  (void) this;
}

public term_T __init_term__ (void) {
  return ClassInit (term,
    .self = SelfInit (term,
      .raw = term_raw,
      .sane = term_sane,
      .restore = term_restore,
      .new = term_new,
      .free  = term_free,
      .get_size = term_get_size,
      .Cursor = SubSelfInit (term, cursor,
        .get_pos = term_get_ptr_pos,
        .set_pos = term_set_ptr_pos,
        .hide = term_cursor_hide,
        .restore = term_cursor_restore
      ),
      .Screen = SubSelfInit (term, screen,
        .restore = term_screen_restore,
        .save = term_screen_save,
        .clear = term_screen_clear,
        .clear_eol = term_screen_clear_eol
      ),
      .Input = SubSelfInit (term, input,
        .get = term_get_input
      )
    )
  );
}

public void __deinit_term__ (term_T *this) {
  (void) this;
}

public video_T __init_video__ (void) {
  return ClassInit (video,
    .self = SelfInit (video,
      .free = video_free,
      .flush = video_flush,
      .new =  video_new,
      .set_with = video_set_row_with,
      .Draw = SubSelfInit (video, draw,
        .row_at = video_draw_at,
        .all = video_draw_all
      )
    )
  );
}

public void __deinit_video__ (video_T *this) {
  (void) this;
}

private ed_T *editor_new (char *name) {
  ed_T *this = Alloc (sizeof (ed_T));
  *this = ClassInit (ed,
    .self = SelfInit (ed,
      .new = ed_new,
      .free = ed_free,
      .free_reg = ed_free_reg,
      .loop = ved_loop,
      .main = ved_main,
      .resume = ed_resume,
      .suspend = ed_suspend,
      .set = SubSelfInit (ed, set,
        .dim = ed_set_dim,
        .screen_size = ed_set_screen_size,
        .current_win = ed_set_current_win,
        .topline = buf_set_topline
      ),
      .get = SubSelfInit (ed, get,
        .bufname = ed_get_bufname,
        .current_buf = ed_get_current_buf,
        .current_win = ed_get_current_win,
        .current_win_idx = ed_get_current_win_idx,
        .state = ed_get_state,
        .win_head = ed_get_win_head,
        .win_next = ed_get_win_next,
        .win_by_idx = ed_get_win_by_idx,
        .next = ed_get_next,
        .prev = ed_get_prev,
      ),
      .append = SubSelfInit (ed, append,
        .win = ed_append_win,
        .message = ed_append_message
      ),
      .readjust = SubSelfInit (ed, readjust,
        .win_size =ed_win_readjust_size
      ),
      .exec = SubSelfInit (ed, exec,
        .cmd = ved_buf_exec_cmd_handler
      ),
      .win = SubSelfInit (ed, win,
        .new = ed_win_new
      ),
    ),
    .Win = ClassInit (win,
      .self = SelfInit (win,
        .set = SubSelfInit (win, set,
          .current_buf = win_set_current_buf,
          .video_dividers = win_set_video_dividers
        ),
        .get = SubSelfInit (win, get,
          .current_buf = win_get_current_buf,
          .buf_by_idx = win_get_buf_by_idx,
          .buf_by_fname = win_get_buf_by_fname,
          .num_buf = win_get_num_buf
        ),
        .adjust = SubSelfInit (win, adjust,
          .buf_dim = win_adjust_buf_dim,
        ),
        .draw = win_draw,
        .buf_new = win_buf_new,
        .buf_new_special = win_buf_new_special,
        .append_buf = win_append_buf
      ),
    ),
    .Buf = ClassInit (buf,
      .self = SelfInit (buf,
        .free = SubSelfInit (buf, free,
          .row = buf_free_row
        ),
        .get = SubSelfInit (buf, get,
          .fname = buf_get_fname
        ),
        .set = SubSelfInit (buf, set,
          .fname = buf_set_fname,
          .video_first_row = buf_set_video_first_row,
          .ftype = buf_set_ftype
        ),
        .to = SubSelfInit (buf, to,
          .video = buf_to_video
        ),
        .cur = SubSelfInit (buf, cur,
          .set = buf_current_set,
          .prepend = buf_current_prepend,
          .append = buf_current_append,
          .append_with = buf_current_append_with,
          .prepend_with = buf_current_prepend_with,
          .delete = buf_current_delete,
          .pop = buf_current_pop
         ),
        .row =  SubSelfInit (buf, row,
          .new_with = buf_row_new_with
        ),
        .read = SubSelfInit (buf, read,
          .fname = buf_read_fname
        ),
        .draw = buf_draw,
        .flush = buf_flush,
        .draw_cur_row = buf_draw_cur_row,
        .append_with = buf_append_with
      ),
    ),
    .Msg = ClassInit (msg,
      .self = SelfInit (msg,
        .send = ed_msg_send,
        .send_fmt = ed_msg_send_fmt,
        .error = ed_msg_error,
        .error_fmt = ed_msg_error_fmt,
        .fmt = ed_msg_fmt
      ),
    ),
    .Error = ClassInit (error,
      .self = SelfInit (error,
        .string = ed_error_string
      ),
    ),
    .Video = __init_video__ (),
    .Term = __init_term__ (),
    .String = __init_string__ (),
    .Cstring = __init_cstring__ (),
    .Re = __init_re__ ()
  );

  this->Cursor.self = this->Term.self.Cursor;
  this->Screen.self = this->Term.self.Screen;
  this->Input.self = this->Term.self.Input;

  strcpy (this->name, name);
  return this;
}

public ed_T *__init_ved__ (void) {
  ed_T *this = editor_new ("ved");

  if (NOTOK is __init__ (this)) {
    this->error_state |= ED_INIT_ERROR;
    return NULL;
  }

  self(new, 1);
  return this;
}

public void __deinit_ved__ (ed_T *Ed) {
  __deinit__ (Ed);

  ed_t *this = Ed->head;
  while (this) {
    ed_t *tmp = this->next;
    self(free);
    this = tmp;
  }

  free (Ed);
}
