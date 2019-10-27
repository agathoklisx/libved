/* A pager for mutt email client, that is being used mainly for testing.
 * The idea is that since this is C, it can be easily extended or modified to
 * suit the needs, plus it receives the libved features and any future development.
 * This "includes" all the editor capabilities including capabilities out of the
 * main machine.

 * By default this splits the window in two frames, with the upper frame to hold
 * the headers, and the lower the body.

 * The body buffer exits with 'q' or with the arrow-left key when the cursor is
 * on the first column.
 * Also by default `space' is treated like PAGE_DOWN (one page forward).

 * The lines are wrapped and the quoted lines are highlighted. It also tries to
 * remove continuously empty lines (more than one), which seems is a habit for some.
 * The default quote char '>', is placed next to each other (if more than one level
 * of quoting).

 * Note that it requires at least 14 screen lines, and with less or on sigwinch
 * that shrinks the window, it will refuse to work or exit, with a violent way.

 * Relative mutt setting:
 *   set pager="path_to_executable"

 * And for the reason stated above, maybe is a good idea for something like the
 * following:
 * macro index <f3>  '<enter-command>set pager=builtin'

 * This builds both a static executable and or an executable which links against
 * the shared library. In the latter case LD_LIBRARY_PATH should point to the lib
 * directory (by default src/sys/library).
 * Both executables are installed in src/sys/bin.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <errno.h>

#include <libved.h>

static ed_T *E = NULL;

#define Ed      E->self
#define Cstring E->Cstring.self
#define Vstring E->Vstring.self
#define Ustring E->Ustring.self
#define String  E->String.self
#define Rline   E->Rline.self
#define Error   E->Error.self
#define Vsys    E->Vsys.self
#define Venv    E->Venv.self
#define Term    E->Term.self
#define Input   E->Input.self
#define File    E->File.self
#define Path    E->Path.self
#define Buf     E->Buf.self
#define Win     E->Win.self
#define Msg     E->Msg.self
#define Dir     E->Dir.self
#define Re      E->Re.self

#define $myed E->current

#include "../../ext/if_has_regexp.c"
#include "../../ext/if_has_shell.c"
#include "../../handlers/sigwinch_handler.c"
#include "../../handlers/alloc_err_handler.c"
#include "../../usr/usr.c"

#ifdef HAS_LOCAL_EXTENSIONS
#include "../../local/local.c"
#endif

char *mail_hdrs_keywords[] = {
  "Subject K", "From K", "To: K", "Date: K", "Reply-To: K", "Cc: K", "cc: K",
  "Message-KD: K", "Message-Kd: K", "User-Agent: K", "X-Mailer: K", "Bc: K", "bc: K",
  "Mail-Followup-To: K", "Reply-to: K", NULL};

char *__mail_NULL_ARRAY[] = {NULL};

private char *__mail_hdrs_syn_parser (buf_t *this, char *line, int len, int idx, row_t *row) {
  return Buf.syn.parser (this, line, len, idx, row);
}

private char *__mail_syn_parser (buf_t *this, char *line, int len, int idx, row_t *row) {
 (void) this; (void) idx; (void) row;
  if (line[0] isnot '>') {
    if (NULL is Cstring.byte_in_str (line, '\t'))
      return line;

    int tabwidth = Buf.get.prop.tabwidth (this);
    string_t *lline = String.new (len + 8);
    for (int i = 0; i < len; i++)
      if (line[i] is '\t') {
        for (int j = 0; j < tabwidth; j++)
          String.append_byte (lline, ' ');
      } else
          String.append_byte (lline, line[i]);

     strncpy (line, lline->bytes, lline->num_bytes);
     line[lline->num_bytes] = '\0';
     String.free (lline);
     return line;
  }

  char lline[len + 16];
  int count = 1;
  while (line[count] is '>') count++;
  switch (count) {
    case 1:
      snprintf (lline, len + 16, "%s%s%s", TERM_MAKE_COLOR(HL_QUOTE),
        line, TERM_COLOR_RESET);
      break;
    case 2:
      snprintf (lline, len + 16, "%s%s%s", TERM_MAKE_COLOR(HL_QUOTE_1),
        line, TERM_COLOR_RESET);
      break;

    case 3:
      snprintf (lline, len + 16, "%s%s%s", TERM_MAKE_COLOR(HL_QUOTE_2),
        line, TERM_COLOR_RESET);
      break;

    case 4:
      snprintf (lline, len + 16, "%s%s%s", TERM_MAKE_COLOR(COLOR_MAGENTA),
        line, TERM_COLOR_RESET);
      break;

    case 5:
      snprintf (lline, len + 16, "%s%s%s", TERM_MAKE_COLOR(COLOR_GREEN),
        line, TERM_COLOR_RESET);
      break;

    case 6:
      snprintf (lline, len + 16, "%s%s%s", TERM_MAKE_COLOR(COLOR_RED),
        line, TERM_COLOR_RESET);
      break;

    default:
      snprintf (lline, len + 16, "%s%s%s", TERM_MAKE_COLOR(COLOR_NORMAL),
        line, TERM_COLOR_RESET);
  }

  strcpy (line, lline);
  return line;
}

private ftype_t *__mail_hdrs_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.init (this, Ed.syn.get_ftype_idx ($myed, "mail_hdrs"), NULL);
  ft->tabwidth = 1;
  ft->on_emptyline[0] = '\0';
  return ft;
}

private ftype_t *__mail_syn_init (buf_t *this) {
  ftype_t *ft= Buf.ftype.init (this, Ed.syn.get_ftype_idx ($myed, "mail"), NULL);
  ft->tabwidth = 4;
  ft->on_emptyline[0] = '\0';
  return ft;
}

syn_t mail_syn[] = {
  {
    "mail_hdrs", __mail_NULL_ARRAY, __mail_NULL_ARRAY, __mail_NULL_ARRAY,
    mail_hdrs_keywords, NULL,
    NULL, NULL, NULL, NULL,
    HL_STRINGS_NO, HL_NUMBERS_NO,
    __mail_hdrs_syn_parser, __mail_hdrs_syn_init, 0, NULL, NULL,
  },
  {
    "mail", __mail_NULL_ARRAY, __mail_NULL_ARRAY, __mail_NULL_ARRAY,
    NULL, NULL,
    NULL, NULL, NULL, NULL,
    HL_STRINGS_NO, HL_NUMBERS_NO,
    __mail_syn_parser, __mail_syn_init, 0, NULL, NULL,
  }
};

private int __on_normal_beg (buf_t **thisp, int com, int *range, int regidx) {
  (void) range; (void) regidx;

  buf_t *this = *thisp;

  switch (com) {
    case ARROW_LEFT_KEY:
      if (Buf.get.row.col_idx (this, Buf.get.row.current (this)) isnot 0)
        return 0;
      // fallthrough

    case 'q':
      return EXIT;

    case ' ':
      Buf.normal.page_down (this, 1);
      return -1;

    default: break;
  }

  return 0;
}

struct buftypes {
  buf_t *headers;
  buf_t *body;
  int
    isemptyline,
    cur_buf;
  size_t num_cols;
};

private int __readlines_cb (vstr_t *lines, char *line, size_t len, int idx,
                                                                   void *obj) {
  (void) lines;
  struct buftypes *bufs = (struct buftypes *) obj;
  if (idx < 3) return 0;

  if (bufs->cur_buf is 0)
    if (len is 1) {
      bufs->cur_buf = 1;
      return 0;
    }

  char sp[(bufs->num_cols * 4) + 1]; /* gcc complains for jumping into scope of identifier
                                      * with variably modified type */

  buf_t *cur_buf = (bufs->cur_buf is 0 ? bufs->headers : bufs->body);

  int has_newline = line[len-1] is '\n';
  char *lline = line;
  if (has_newline) {
    lline = Cstring.trim.end (line, '\n');
    len--;
    ifnot (len) {
      if (bufs->isemptyline) return 0;
      Buf.cur.append_with (cur_buf, NULL);
      return 0;
    }
  }

  if (bufs->cur_buf is 0) {
    Buf.cur.append_with (bufs->headers, lline);
    goto theend;
  }

  if (cur_buf is bufs->body) {
    if (len is 0 or
       (len is 1 and (lline[0] is ' ' or lline[0] is '>')) or
        Cstring.eq (lline, ">>") or Cstring.eq (lline, ">>>")) {

      if (bufs->isemptyline) return 0;
      bufs->isemptyline = 1;
    } else
      bufs->isemptyline = 0;
  }

  size_t numchars = 0;
  size_t j = 0;
  size_t k = 0;
  size_t cur_idx = 0;
  int quot_num = 0;
  char quot_buf[64]; quot_buf[0] = '\0';

  if (cur_buf is bufs->body and lline[0] is '>') {
    for (; cur_idx < len and cur_idx < 64; cur_idx++) {
      if ('>' is lline[cur_idx]) {
        quot_buf[quot_num++] = '>';
        numchars++;
        continue;
      }

      if (' ' is lline[cur_idx]) continue;
      break;
    }

    if (quot_num) {
      if (line[cur_idx] isnot ' ')
        quot_buf[quot_num++] = ' ';
      quot_buf[quot_num] = '\0';
      Buf.cur.append_with_len (cur_buf, quot_buf, quot_num);
    }
  }

  int tabwidth = Buf.get.prop.tabwidth (cur_buf);

  u8_t *uline = Ustring.new ();
  Ustring.encode (uline, lline, len, DONOT_CLEAR, tabwidth, 0);

  vchar_t *it = uline->head;
  vchar_t *tmp = it;

  if (cur_idx > 0) /* humanized expression */
    for (size_t i = 0; i < cur_idx; i++)
      it = it->next;

  int num_iterations = 0;

  while (it) {
    if (num_iterations) j = numchars = 0 ;
    sp[0] = '\0';

    if (cur_buf is bufs->body) {
      if (num_iterations and quot_num) {
        Buf.cur.append_with_len (cur_buf, quot_buf, quot_num);
        numchars += quot_num;
      }
    }

    while (it and numchars < bufs->num_cols) {
      for (int i = 0; i < it->len; i++)
        sp[j++] = it->buf[i];

      numchars += it->width;
      tmp = it;
      it = it->next;
    }

    num_iterations++;
    sp[j] = '\0';

    if (cur_buf is bufs->body and quot_num)
      String.append_with_len (Buf.get.row.current_bytes (cur_buf), sp, j);
    else
      Buf.cur.append_with (cur_buf, sp);
  }

  k = j;

  it = tmp;
  while (numchars > bufs->num_cols) {
    j -= it->len;
    numchars -= it->width;
    it = it->prev;
  }

  if (k isnot j) {
    sp[j] = '\0';
    Buf.cur.replace_with (cur_buf, sp);
     sp[0] = '\0';
    j = 0;

    if (cur_buf is bufs->body and quot_num) {
      Buf.cur.append_with_len (cur_buf, quot_buf, quot_num);
      numchars += quot_num;
    }

    for (int i = 0; i < tmp->len; i++) sp[j++] = tmp->buf[i];
    sp[j] = '\0';

    if (cur_buf is bufs->body)
      if (Cstring.eq (sp, quot_buf)) goto deallocate;

    Buf.cur.append_with (cur_buf, sp);
  }

deallocate:
  Ustring.free (uline);

theend:
  return 0;
}

private dim_t **__win_dim_calc_cb (win_t *this, int num_rows, int num_frames, int min_rows,
                                                            int has_dividers) {
  (void) this; (void) has_dividers;
  int reserved =  3;
  int rows = (num_frames * min_rows) + reserved;

  if (num_rows < rows) return NULL;

  rows = num_rows - reserved;
  int num_cols = Win.get.num_cols (this);
  dim_t **dims = Ed.dims_init ($myed, num_frames);

  dims[0] = Ed.set.dim ($myed, dims[0], 2, 8, 1, num_cols);
  dims[1] = Ed.set.dim ($myed, dims[1], 9, 9 + (rows - 8), 1, num_cols);
  return dims;
}

private win_t *__init_me__ (ed_t *this, char *fname) {
  win_t *w = Ed.win.init (this, "mail", __win_dim_calc_cb);
  Win.set.num_frames (w, 2);
  Win.set.min_rows (w, 7);
  Win.set.has_dividers (w, UNSET);
  Win.dim_calc (w);

  Ed.append.win (this, w);

  Ed.syn.append (this, mail_syn[0]);
  Ed.syn.append (this, mail_syn[1]);

  struct buftypes buffers;
  buffers.cur_buf = 0;
  buffers.isemptyline = 0;
  buffers.num_cols = Win.get.num_cols (w);

  buf_t *buf = Win.buf.init (w, FIRST_FRAME, ZERO_FLAGS);
  Buf.set.show_statusline (buf, UNSET);
  Buf.set.fname (buf, UNAMED);
  Buf.set.ftype (buf, Ed.syn.get_ftype_idx ($myed, "mail_hdrs"));
  Buf.set.normal.at_beg_cb (buf, __on_normal_beg);
  Win.append_buf (w, buf);
  buffers.headers = buf;

  buf_t *bbody = Win.buf.init (w, SECOND_FRAME, ZERO_FLAGS);
  Buf.set.show_statusline (bbody, UNSET);
  Buf.set.fname (bbody, fname);
  Buf.set.ftype (bbody, Ed.syn.get_ftype_idx ($myed, "mail"));
  Buf.set.normal.at_beg_cb (bbody, __on_normal_beg);
  Win.append_buf (w, bbody);
  buffers.body = bbody;

  vstr_t lines;
  File.readlines (fname, &lines, __readlines_cb, &buffers);

  Buf.set.row.idx (buf, 0, NO_OFFSET, 1);
  Buf.set.row.idx (bbody, 0, NO_OFFSET, 1);
  Win.set.current_buf (w, 0, DONOT_DRAW);
  Win.set.current_buf (w, 1, DONOT_DRAW);
  Win.draw (w);

  return w;
}

int main (int argc, char **argv) {
  if (1 is argc) return 1;

  if (0 is isatty (fileno (stdout)) or 0 is isatty (fileno (stdin))) {
    tostderr ("Not a controlled terminal\n");
    exit (1);
  }

  setlocale (LC_ALL, "");

  AllocErrorHandler = __alloc_error_handler__;

  ++argv; --argc;

  if (NULL is (E = __init_ed__ ())) return 1;

  Re.exec = my_re_exec;
  Re.parse_substitute = my_re_parse_substitute;
  Re.compile = my_re_compile;
  Ed.sh.popen = my_ed_sh_popen;

  ed_t *this = Ed.init (E);

  __init_usr__ (this);

#ifdef HAS_LOCAL_EXTENSIONS
  __init_local__ (this);
#endif

  int retval = 1;

  Ed.history.read (this, VED_DATA_DIR);

  win_t *w = __init_me__ (this, argv[0]);

  signal (SIGWINCH, sigwinch_handler);

  for (;;) {
    buf_t *buf = Ed.get.current_buf (this);
    retval = Ed.main (this, buf);

    if (Ed.get.state (this) & ED_SUSPENDED) {
      if (E->num_items is 1) {
        this = Ed.new (E, 1);
        w = Ed.get.current_win (this);
        buf = Win.buf.new (w, NULL, FIRST_FRAME, ZERO_FLAGS);
        Win.append_buf (w, buf);
        Win.set.current_buf (w, 0, DRAW);
      } else {
        this = Ed.get.prev (this);
      }
    } else break;
  }

 Ed.history.write (this, VED_DATA_DIR);

  __deinit_usr__ (this);

#ifdef HAS_LOCAL_EXTENSIONS
  __deinit_local__ (this);
#endif

  __deinit_ed__ (E);

  return retval;
}
