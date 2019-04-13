#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include <libved.h>

ed_T *E = NULL;

#define Ed E->self
#undef My
#define My(__C__) E->__C__.self

#ifdef HAS_REGEXP
#include "modules/slre/slre.h"
#include "modules/slre/slre.c"

/* this is like slre_match(), with an aditional argument and two extra fields
 * in the slre regex_info structure */
int re_match (regexp_t *re, const char *regexp, const char *s, int s_len,
struct slre_cap *caps, int num_caps, int flags) {
  struct regex_info info;

  info.flags = flags;
  info.num_brackets = info.num_branches = 0;
  info.num_caps = num_caps;
  info.caps = caps;

  info.match_idx = info.match_len = -1;

  int retval = foo (regexp, (int) strlen(regexp), s, s_len, &info);
  if (0 <= retval) {
    re->match_idx = info.match_idx;
    re->match_len = info.match_len;
    re->match_ptr = (char *) s + info.match_idx;
  }

  return retval;
}

private int my_re_compile (regexp_t *re) {
  ifnot (My(Cstring).cmp_n (re->pat->bytes, "(?i)", 4)) {
    re->flags |= RE_IGNORE_CASE;
    My(String).delete_numbytes_at (re->pat, 4, 0);
  }

  return OK;
}

private int my_re_exec (regexp_t *re, char *buf, size_t buf_len) {
  re->retval = RE_NO_MATCH;
  if (re->pat->num_bytes is 1 and
     (re->pat->bytes[0] is '^' or
      re->pat->bytes[0] is '$' or
      re->pat->bytes[0] is '|'))
    return re->retval;

  struct slre_cap cap[re->num_caps];
  for (int i = 0; i < re->num_caps; i++) cap[i].len = 0;

  re->retval = re_match (re, re->pat->bytes, buf, buf_len,
      cap, re->num_caps, re->flags);

  if (0 <= re->retval) {
    re->match = My(String).new_with (re->match_ptr);
    My(String).clear_at (re->match, re->match_len);

    for (int i = 0; i < re->num_caps; i++) {
      ifnot (cap[i].len) break;
      re->cap[i] = AllocType (capture);
      re->cap[i]->ptr = cap[i].ptr;
      re->cap[i]->len = cap[i].len;
    }
  }

  return re->retval;
}

private string_t *my_re_parse_substitute (regexp_t *re, char *sub, char *replace_buf) {
  string_t *substr = My(String).new ();
  char *sub_p = sub;
  while (*sub_p) {
    switch (*sub_p) {
      case '\\':
        if (*(sub_p + 1) is 0) goto theerror;

        switch (*++sub_p) {
          case '&':
            My(String).append_byte (substr, '&');
            sub_p++;
            continue;

          case '\\':
            My(String).append_byte (substr, '\\');
            sub_p++;
            continue;

          case '1'...'9':
            {
              int idx = *sub_p - '0' - 1;
              if (0 > idx or idx + 1 > re->num_caps)
                goto theerror;

              char buf[re->cap[idx]->len + 1];
              memcpy (buf, re->cap[idx]->ptr, re->cap[idx]->len);
              buf[re->cap[idx]->len] = '\0';
              My(String).append (substr, buf);
            }
            sub_p++;
            continue;

          default:
            goto theerror;
        }

      case '&':
        My(String).append (substr, replace_buf);
        break;

      default:
        My(String).append_byte (substr, *sub_p);
     }

    sub_p++;
  }

  return substr;

theerror:
  My(String).free (substr);
  return NULL;
}

#endif

private void sigwinch_handler (int sig) {
  (void) sig;
  ed_t *ed = E->head;
  while (ed) {
    Ed.set.screen_size (ed);
    win_t *w = Ed.get.win_head (ed);
    while (w) {
      Ed.readjust.win_size (ed, w);
      w = Ed.get.win_next (ed, w);
    }

    ed = Ed.get.next (ed);
  }
}

mutable public void __alloc_error_handler__ (
int err, size_t size,  char *file, const char *func, int line) {
  fprintf (stderr, "MEMORY_ALLOCATION_ERROR\n");
  fprintf (stderr, "File: %s\nFunction: %s\nLine: %d\n", file, func, line);
  fprintf (stderr, "Size: %zd\n", size);

  if (err is INTEGEROVERFLOW_ERROR)
    fprintf (stderr, "Error: Integer Overflow Error\n");
  else
    fprintf (stderr, "Error: Not Enouch Memory\n");

  ifnot (NULL is E) __deinit_ved__ (E);
  exit (1);
}

int main (int argc, char **argv) {

  AllocErrorHandler = __alloc_error_handler__;

  ++argv; --argc;

  if (NULL is (E = __init_ved__ ()))
    return 1;

#ifdef HAS_REGEXP
  My(Re).exec = my_re_exec;
  My(Re).parse_substitute = my_re_parse_substitute;
  My(Re).compile = my_re_compile;
#endif

  ed_t *this = E->current;
  win_t *w = Ed.get.current_win (this);

  ifnot (argc) {
     buf_t *buf = My(Win).buf_new (w, NULL, 0);
     My(Win).append_buf (w, buf);
  } else
    for (int i = 0; i < argc; i++) {
      buf_t *buf = My(Win).buf_new (w, argv[i], 0);
      My(Win).append_buf (w, buf);
    }

  My(Win).set.current_buf (w, 0);

  int retval = 0;
  signal (SIGWINCH, sigwinch_handler);

  for (;;) {
    buf_t *buf = Ed.get.current_buf (this);
    retval = Ed.main (this, buf);

    if (Ed.get.state (this) & ED_SUSPENDED) {
      if (E->num_items is 1) {
        Ed.new (E, 1);
        this = E->current;
        w = Ed.get.current_win (this);
        buf = My(Win).buf_new (w, NULL, 0);
        My(Win).append_buf (w, buf);
        My(Win).set.current_buf (w, 0);
      } else {
        ed_t *ed = Ed.get.next (this);
        if (NULL is ed) ed = Ed.get.prev (this);
        this = ed;
      }
    } else
      break;
  }

  __deinit_ved__ (E);

  return retval;
}
