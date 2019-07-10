#include "../modules/slre/slre.h"
#include "../modules/slre/slre.c"

/* this is like slre_match(), with an aditional argument and three extra fields
 * in the slre regex_info structure */
int re_match (regexp_t *re, const char *regexp, const char *s, int s_len,
                            struct slre_cap *caps, int num_caps, int flags) {
  struct regex_info info;

  info.flags = flags;
  info.num_brackets = info.num_branches = 0;
  info.num_caps = num_caps;
  info.caps = caps;

  info.match_idx = info.match_len = -1;
  info.total_caps = 0;

  int retval = foo (regexp, (int) strlen(regexp), s, s_len, &info);
  if (0 <= retval) {
    re->match_idx = info.match_idx;
    re->match_len = info.match_len;
    re->total_caps = info.total_caps;
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

  do {
    struct slre_cap cap[re->num_caps];
    for (int i = 0; i < re->num_caps; i++) cap[i].len = 0;

    re->retval = re_match (re, re->pat->bytes, buf, buf_len,
        cap, re->num_caps, re->flags);

    if (re->retval is RE_CAPS_ARRAY_TOO_SMALL_ERROR) {
      My(Re).free_captures (re);
      My(Re).allocate_captures (re, re->num_caps + (re->num_caps / 2));
      continue;
    }

    if (0 > re->retval) return re->retval;

    re->match = My(String).new_with (re->match_ptr);
    My(String).clear_at (re->match, re->match_len);

    for (int i = 0; i < re->total_caps; i++) {
      re->cap[i] = AllocType (capture);
      re->cap[i]->ptr = cap[i].ptr;
      re->cap[i]->len = cap[i].len;
    }
  } while (0);

  return re->retval;
}

private string_t *my_re_parse_substitute (regexp_t *re, char *sub, char *replace_buf) {
  string_t *substr = My(String).new_with (NULL);
  char *sub_p = sub;
  while (*sub_p) {
    switch (*sub_p) {
      case '\\':
        if (*(sub_p + 1) is 0) {
          strcpy (re->errmsg, "awaiting escaped char, found (null byte) 0");
          goto theerror;
        }

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
              int idx = 0;
              while (*sub_p and ('0' <= *sub_p and *sub_p <= '9')) {
                idx = (10 * idx) + (*sub_p - '0');
                sub_p++;
              }
              idx--;
              if (0 > idx or idx + 1 > re->total_caps) goto theerror;

              char buf[re->cap[idx]->len + 1];
              memcpy (buf, re->cap[idx]->ptr, re->cap[idx]->len);
              buf[re->cap[idx]->len] = '\0';
              My(String).append (substr, buf);
            }

            continue;

          default:
            strcpy (re->errmsg, "awaiting \\,&,[0..9,...]");
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

/*
#ifdef HAS_REGEXP
  My(Re).exec = my_re_exec;
  My(Re).parse_substitute = my_re_parse_substitute;
  My(Re).compile = my_re_compile;
#endif
*/
