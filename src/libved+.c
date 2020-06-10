#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <pty.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <locale.h>
#include <errno.h>

#include "libved.h"
#include "libved+.h"
#include "lib/lib+.h"

public Class (This) *__THIS__ = NULL;
public Self (This)  *__SELF__ = NULL;
public Class (I)    *__I__    = NULL;

#if HAS_RUNTIME_INTERPRETER
public Class (L)    *__L__    = NULL;
#endif

#include "handlers/sigwinch_handler.c"
#include "handlers/alloc_err_handler.c"

#if HAS_SHELL_COMMANDS
  #include "ext/if_has_shell.c"
#endif

#if HAS_USER_EXTENSIONS
  #include "usr/usr.c"
#endif

#if HAS_LOCAL_EXTENSIONS
  #include "local/local.c"
#endif

#if HAS_REGEXP
  #include "ext/if_has_regexp.c"
#endif

private string_t *__ex_buf_serial_info__ (bufinfo_t *info) {
  string_t *sinfo = String.new_with ("BUF_INFO_STRUCTURE\n");
  String.append_fmt (sinfo,
    "fname       : \"%s\"\n"
    "cwd         : \"%s\"\n"
    "parent name : \"%s\"\n"
    "at frame    : %d\n"
    "num bytes   : %zd\n"
    "num lines   : %zd\n"
    "cur idx     : %d\n"
    "is writable : %d\n",
    info->fname, info->cwd, info->parents_name, info->at_frame,
    info->num_bytes, info->num_lines, info->cur_idx, info->is_writable);

  return sinfo;
}

private string_t *__ex_win_serial_info__ (wininfo_t *info) {
  string_t *sinfo = String.new_with ("WIN_INFO_STRUCTURE\n");
  String.append_fmt (sinfo,
    "name         : \"%s\"\n"
    "ed name      : \"%s\"\n"
    "num buf      : %zd\n"
    "num frames   : %d\n"
    "cur buf idx  : %d\n"
    "cur buf name : \"%s\"\n"
    "buf names    :\n",
    info->name, info->parents_name, info->num_items, info->num_frames,
    info->cur_idx, info->cur_buf);

  for (size_t i = 0; i < info->num_items; i++)
    String.append_fmt (sinfo, "%12d : \"%s\"\n", i + 1, info->buf_names[i]);

  return sinfo;
}

private string_t *__ex_ed_serial_info__ (edinfo_t *info) {
  string_t *sinfo = String.new_with ("ED_INFO_STRUCTURE\n");
  String.append_fmt (sinfo,
    "name         : \"%s\"\n"
    "normal win   : %zd\n"
    "special win  : %d\n"
    "cur win idx  : %d\n"
    "cur win name : \"%s\"\n"
    "win names    :\n",
    info->name, info->num_items, info->num_special_win, info->cur_idx,
    info->cur_win);

  for (size_t i = 0; i < info->num_items; i++)
    String.append_fmt (sinfo, "%12d : \"%s\"\n", i + 1, info->win_names[i]);
  return sinfo;
}

private int __ex_com_info__ (buf_t **thisp, rline_t *rl) {
  (void) thisp; (void) rl;
  ed_t *ced = E(get.current);

  int
    buf = Rline.arg.exists (rl, "buf"),
    win = Rline.arg.exists (rl, "win"),
    ed  = Rline.arg.exists (rl, "ed");

  ifnot (buf + win + ed) buf = 1;

  Ed.append.toscratch (ced, CLEAR, "");

  if (buf) {
    bufinfo_t *binfo = Buf.get.info.as_type (*thisp);
    string_t *sbinfo = __ex_buf_serial_info__ (binfo);
    Ed.append.toscratch (ced, DONOT_CLEAR, sbinfo->bytes);
    String.free (sbinfo);
    Buf.free.info (*thisp, &binfo);
  }

  if (win) {
    win_t *cw = Ed.get.current_win (ced);
    wininfo_t *winfo = Win.get.info.as_type (cw);
    string_t *swinfo = __ex_win_serial_info__ (winfo);
    Ed.append.toscratch (ced, DONOT_CLEAR, swinfo->bytes);
    String.free (swinfo);
    Win.free_info (cw, &winfo);
  }

  if (ed) {
    edinfo_t *einfo = Ed.get.info.as_type (ced);
    string_t *seinfo = __ex_ed_serial_info__ (einfo);
    Ed.append.toscratch (ced, DONOT_CLEAR, seinfo->bytes);
    String.free (seinfo);
    Ed.free_info (ced, &einfo);
  }

  Ed.scratch (ced, thisp, NOT_AT_EOF);

  return OK;
}

private int __ex_rline_cb__ (buf_t **thisp, rline_t *rl, utf8 c) {
  (void) thisp; (void) c;
  int retval = RLINE_NO_COMMAND;
  string_t *com = Rline.get.command (rl);

  if (Cstring.eq (com->bytes, "@info")) {
    retval = __ex_com_info__ (thisp, rl);
    goto theend;
  }

theend:
  String.free (com);
  return retval;
}

private void __ex_add_rline_commands__ (ed_t *this) {
  int num_commands = 1;
  char *commands[] = {"@info", NULL};
  int num_args[] = {0, 0};
  int flags[] = {0, 0};
  Ed.append.rline_commands (this, commands, num_commands, num_args, flags);
  Ed.append.command_arg (this, "@info", "--buf", 5);
  Ed.append.command_arg (this, "@info", "--win", 5);
  Ed.append.command_arg (this, "@info", "--ed", 5);
  Ed.set.rline_cb (this, __ex_rline_cb__);
}

private void __init_ext__ (Type (ed) *this) {
  __ex_add_rline_commands__ (this);

#if HAS_REGEXP
  Re.exec = ext_re_exec;
  Re.parse_substitute =ext_re_parse_substitute;
  Re.compile = ext_re_compile;
#endif

#if HAS_SHELL_COMMANDS
  Ed.sh.popen = ext_ed_sh_popen;
#endif

#if HAS_USER_EXTENSIONS
  __init_usr__ (this);
#endif

#if HAS_LOCAL_EXTENSIONS
  __init_local__ (this);
#endif

#ifdef HAS_HISTORY
  Ed.history.read (this);
  Ed.set.at_exit_cb (this, Ed.history.write);
#endif
}

private void __deinit_ext__ (void) {
#if HAS_USER_EXTENSIONS
  __deinit_usr__ ();
#endif

#if HAS_LOCAL_EXTENSIONS
  __deinit_local__ ();
#endif
}

private string_t *this_parse_command (Class (This) *this, char *bytes) {
  (void) this;
  string_t *com = String.new (256);
  char *sp = bytes;
  while (*sp) {
    if (*sp isnot ':')
      String.append_byte (com, *sp);
    else {
      if (*(sp+1) isnot ':')
        String.append_byte (com, *sp);
      else {
        String.append_byte (com, ' ');
        sp++;
      }
    }
    sp++;
  }

  return com;
}

private int parse_arg_init (Class (This) *this, argparse_t *argparser,
   argparse_option_t *argparser_options, const char *const *usage_cb, int flags) {
  (void) this;
  return argparse_init (argparser, argparser_options, usage_cb, flags);
}

private int parse_arg_run (Class (This) *this, argparse_t *argparser, int argc,
                                                             const char **argv) {
  (void) this;
  return argparse_parse (argparser, argc, argv);
}

private void e_set_at_exit_cb (Class (This) *this, EAtExit_cb cb) {
  __E.set.at_exit_cb (this->__E__, cb);
}

private int e_get_num (Class (This) *this) {
  return __E.get.num (this->__E__);
}

private int e_get_prev_idx (Class (This) *this) {
  return __E.get.prev_idx (this->__E__);
}

private int e_get_current_idx (Class (This) *this) {
  return __E.get.current_idx (this->__E__);
}

private ed_t *e_get_current (Class (This) *this) {
  return __E.get.current (this->__E__);
}

private ed_t *e_get_head (Class (This) *this) {
  return __E.get.head (this->__E__);
}

private ed_t *e_get_next (Class (This) *this, ed_t *ed) {
  return __E.get.next (this->__E__, ed);
}

private string_t *e_get_env (Class (This) *this, char *name) {
  return __E.get.env (this->__E__, name);
}

private int e_get_state (Class (This) *this) {
  return __E.get.state (this->__E__);
}

private ed_t * e_set_current (Class (This) *this, int idx) {
  return __E.set.current (this->__E__, idx);
}

private ed_t *e_set_next (Class (This) *this) {
   return __E.set.next (this->__E__);
}

private ed_t *e_init (Class (This) *this, EdAtInit_cb cb) {
  return __E.init (this->__E__, cb);
}

private ed_t* e_new (Class (This) *this, ED_INIT_OPTS opts) {
  return __E.new (this->__E__, opts);
}

private int e_main (Class (This) *this, buf_t *buf) {
  return __E.main (this->__E__, buf);
}

private void i_print_bytes (FILE *fp, const char *bytes) {
  fprintf (fp, "%s", bytes);
}

private void i_print_fmt_bytes (FILE *fp, const char *fmt, ...) {
  size_t len = VA_ARGS_FMT_SIZE (fmt);
  char bytes[len + 1];
  VA_ARGS_GET_FMT_STR(bytes, len, fmt);
  i_print_bytes (fp, bytes);
}

private void i_print_byte (FILE *fp, int c) {
  i_print_fmt_bytes (fp, "%c", c);
}

ival_t i_init_this (void) {
  return (ival_t) __init_this__ ();
}

ival_t i_deinit_this (void) {
  __deinit_this__ (&__THIS__);
  return OK;
}

ival_t i_get_ed_num (void) {
  return E(get.num);
}

ival_t i_set_ed_next (void) {
  return (ival_t) E(set.next);
}

ival_t i_set_ed_by_idx (ival_t inst, int idx) {
  (void) inst;
  return (ival_t) E(set.current, idx);
}

ival_t i_get_ed_current_idx (void) {
  return E(get.current_idx);
}

ival_t i_ed_new (ival_t inst, int num_win) {
  (void) inst;
  return (ival_t) E(new, QUAL(ED_INIT, .num_win = num_win, .init_cb = __init_ext__));
}

ival_t i_ed_get_num_win (ival_t inst, ed_t *ed) {
  (void) inst;
  return Ed.get.num_win (ed, 0);
}

ival_t i_ed_get_current_win (ival_t inst, ed_t *ed) {
  (void) inst;
  return (ival_t) Ed.get.current_win (ed);
}

ival_t i_ed_get_win_next (ival_t inst, ed_t *ed, win_t *win) {
  (void) inst;
  return (ival_t) Ed.get.win_next (ed, win);
}

ival_t i_buf_init_fname (ival_t inst, buf_t *this) {
  char *fn = i_pop_string (inst);
  Buf.init_fname (this, fn);
  free (fn);
  return OK;
}

ival_t i_buf_set_ftype (ival_t inst, buf_t *this) {
  char *ftype = i_pop_string (inst);
  Buf.set.ftype (this, Ed.syn.get_ftype_idx (E(get.current), ftype));
  free (ftype);
  return OK;
}

ival_t i_buf_set_row_idx (ival_t inst, buf_t *this, int row) {
  (void) inst;
  Buf.set.row.idx (this, row, NO_OFFSET, 1);
  return OK;
}

ival_t i_buf_draw (ival_t inst, buf_t *this) {
  (void) inst;
  Buf.draw (this);
  return OK;
}

ival_t i_win_buf_init (ival_t inst, win_t *this, int frame, int flags) {
  (void) inst;
  return (ival_t) Win.buf.init (this, frame, flags);
}

ival_t i_win_set_current_buf (ival_t inst, win_t *this, int idx, int draw) {
  (void) inst;
  buf_t *buf = Win.set.current_buf (this, idx, draw);
  Buf.get.fname (buf);
  return (ival_t) buf;
}

ival_t i_buf_normal_page_down (ival_t inst, buf_t *this, int count) {
  (void) inst;
  return Buf.normal.page_down (this, count);
}

ival_t i_buf_normal_page_up (ival_t inst, buf_t *this, int count) {
  (void) inst;
  return Buf.normal.page_up (this, count);
}

ival_t i_buf_normal_goto_linenr (ival_t inst, buf_t *this, int linenum, int draw) {
  (void) inst;
  return Buf.normal.goto_linenr (this, linenum, draw);
}

ival_t i_win_draw (ival_t inst, win_t *this) {
  (void) inst;
  Win.draw (this);
  return OK;
}

ival_t i_win_append_buf (ival_t inst, win_t *this, buf_t *buf) {
  (void) inst;
  Win.append_buf (this, buf);
  return OK;
}

struct ifun_t {
  const char *name;
  ival_t val;
  int nargs;
} ifuns[] = {
  { "init_this",           (ival_t) i_init_this, 0},
  { "deinit_this",         (ival_t) i_deinit_this, 0},
  { "get_ed_num",          (ival_t) i_get_ed_num, 0},
  { "set_ed_next",         (ival_t) i_set_ed_next, 0},
  { "set_ed_by_idx",       (ival_t) i_set_ed_by_idx, 1},
  { "get_ed_current_idx",  (ival_t) i_get_ed_current_idx, 0},
  { "ed_new",              (ival_t) i_ed_new, 1},
  { "ed_get_num_win",      (ival_t) i_ed_get_num_win, 1},
  { "ed_get_current_win",  (ival_t) i_ed_get_current_win, 1},
  { "ed_get_win_next",     (ival_t) i_ed_get_win_next, 2},
  { "buf_init_fname",      (ival_t) i_buf_init_fname, 2},
  { "buf_set_ftype",       (ival_t) i_buf_set_ftype, 2},
  { "buf_set_row_idx",     (ival_t) i_buf_set_row_idx, 2},
  { "buf_draw",            (ival_t) i_buf_draw, 1},
  { "buf_normal_page_down",(ival_t) i_buf_normal_page_down, 2},
  { "buf_normal_page_up",  (ival_t) i_buf_normal_page_up, 2},
  { "buf_normal_goto_linenr",(ival_t) i_buf_normal_goto_linenr, 3},
  { "win_buf_init",        (ival_t) i_win_buf_init, 3},
  { "win_draw",            (ival_t) i_win_draw, 1},
  { "win_append_buf",      (ival_t) i_win_append_buf, 2},
  { "win_set_current_buf", (ival_t) i_win_set_current_buf, 3},
  { NULL, 0, 0}
};

private int i_load_file (Class (This) *__t__, char *fn) {
  (void) __t__;
  i_t *in = This(i.init_instance, __I__);

  ifnot (Path.is_absolute (fn)) {
    size_t fnlen = bytelen (fn);
    char fname[fnlen+3];
    Cstring.cp (fname, fnlen + 1, fn, fnlen);
    char *extname = Path.extname (fname);
    ifnot (extname[0]) {
      fname[fnlen] = '.';
      fname[fnlen+1] = 'i';
      fname[fnlen+2] = '\0';
    }

    if (File.exists (fname))
      return In.eval_file (in, fname);

    string_t *ddir = E(get.env, "data_dir");
    size_t len = ddir->num_bytes + bytelen (fname) + 2 + 8;
    char tmp[len + 1];
    Cstring.cp_fmt (tmp, len + 1, "%s/profiles/%s", ddir->bytes, fname);
    ifnot (File.exists (tmp))
      return NOTOK;
    return In.eval_file (in, tmp);
  } else
    return In.eval_file (in, fn);

  return OK;
}

private i_t *i_init_instance (Class (This) *__t__, Class (I) *__i__) {
  (void) __t__;
  i_t *this = In.new ();

  FILE *err_fp = stderr;

#if DEBUG_INTERPRETER
  string_t *tdir = E(get.env, "tmp_dir");
  size_t len = tdir->num_bytes + 1 + 7;
  char tmp[len + 1 ];
  Cstring.cp_fmt (tmp, len + 1, "%s/i.debug", tdir->bytes);
  err_fp = fopen (tmp, "w");
#endif

  In.init (__i__, this, QUAL(I_INIT,
    .mem_size = 8192,
    .print_bytes = i_print_bytes,
    .print_byte  = i_print_byte,
    .print_fmt_bytes = i_print_fmt_bytes,
    .err_fp = err_fp
  ));

  int err = 0;
  for (int i = 0; ifuns[i].name; i++) {
    err = In.def (this, ifuns[i].name, CFUNC (ifuns[i].nargs), ifuns[i].val);
    if (err isnot I_OK) {
      __deinit_this__ (&__THIS__);
      fprintf (stderr, "Error while initializing the interpreter\n");
      exit (1);
    }
  }

  return this;
}

#if HAS_RUNTIME_INTERPRETER

private Lstate *__init_lstate__ (const char *src, int argc, const char **argv) {
  Lstate *this = initVM (0, src, argc, argv);
  __init_led__ (this);
  return this;
}

private void __deinit_lstate__ (Lstate **thisp) {
  if (NULL is *thisp) return;
  freeVM (*thisp);
  *thisp = NULL;
}

public Class (L) *__init_l__ (int num_states) {
  Class (L) *this =  Alloc (sizeof (Class (L)));

  this->num_states = num_states;
  this->states = Alloc (vm_sizeof () * this->num_states);
  this->cur_state = 0;
  return this;
}

public void __deinit_l__ (Class (L) **thisp) {
  if (NULL is *thisp) return;
  Class (L) *this = *thisp;

  for (int i = 0; i < this->num_states; i++)
    __deinit_lstate__ (&this->states[i]);

  free (this->states);
  free (this);
  *thisp = NULL;
}

#endif /* HAS_RUNTIME_INTERPRETER */

private void __init_self__ (Class (This) *this) {
  this->self = AllocSelf (This);

  SubSelf (This, parse) parse = SubSelfInit (This, parse,
    .command = this_parse_command,
    .arg = SubSelfInit (Thisparse, arg,
      .init = parse_arg_init,
      .run =  parse_arg_run
    )
  );
  ((Self (This) *) this->self)->parse = parse;

  SubSelf (This, e) e = SubSelfInit (This, e,
    .init = e_init,
    .new = e_new,
    .main = e_main,
    .set = SubSelfInit (Thise, set,
      .at_exit_cb = e_set_at_exit_cb,
      .current = e_set_current,
      .next = e_set_next
    ),
    .get = SubSelfInit (Thise, get,
      .num = e_get_num,
      .prev_idx = e_get_prev_idx,
      .state = e_get_state,
      .current = e_get_current,
      .current_idx = e_get_current_idx,
      .head = e_get_head,
      .next = e_get_next,
      .env = e_get_env
    )
  );
  ((Self (This) *) this->self)->e = e;

  SubSelf (This, i) in = SubSelfInit (This, i,
    .init_instance = i_init_instance,
    .load_file = i_load_file
  );
  ((Self (This) *) this->self)->i = in;

#if HAS_RUNTIME_INTERPRETER
  SubSelf (This, l) l = SubSelfInit (This, l,
    .init = __init_lstate__,
    .deinit = __deinit_lstate__,
    .compile = interpret,
    .newString = copyString,
    .defineFun = defineNative,
    .defineProp = defineNativeProperty,
    .table = SubSelfInit (Thisl, table,
      .get = tableGet
    )
  );

  ((Self (This) *) this->self)->l = l;
#endif

#if HAS_SHELL_COMMANDS
  Self (proc) p = __init_proc__ ().self;
  ((Self (This) *) this->self)->proc = p;
#endif

}

private int __initialize__ (void) {
  /* I do not know the way to read from stdin and at the same time to
   * initialize and use the terminal state, when we are the end of the pipe */
  if (0 is isatty (fileno (stdout)) or 0 is isatty (fileno (stdin))) {
    tostderr ("Not a controlled terminal\n");
    return NOTOK;
  }

  setlocale (LC_ALL, "");
  AllocErrorHandler = __alloc_error_handler__;
  return OK;
}

public Class (This) *__init_this__ (void) {
  E_T *__e__;
  if (NOTOK is __initialize__ () or
      NULL is (__e__ = __init_ed__ (MYNAME)))
    return NULL;

  Class (This) *this = AllocClass (This);

  __THIS__ = this;
  __THIS__->__E__ = __e__;
  __THIS__->__E__->__THIS__ =  __THIS__;

  __init_self__ (__THIS__);
  __SELF__ = __THIS__->self;

  __I__ = __init_i__ ();

  ((Prop (This) *) $myprop)->__I__ = __I__;

#if HAS_RUNTIME_INTERPRETER
  __L__ = __init_l__ (1);
  ((Prop (This) *) $myprop)->__L__ = __L__;
  __L__->self = __SELF__->l;
  L_CUR_STATE = L.init ("__global__", 0, NULL);
#endif

  return __THIS__;
}

public void __deinit_this__ (Class (This) **thisp) {
  if (*thisp is NULL) return;

  Class (This) *this = *thisp;

  __deinit_ed__ (&this->__E__);

  free (__SELF__);

  __deinit_i__ (&__I__);

#if HAS_RUNTIME_INTERPRETER
  __deinit_l__ (&__L__);
#endif

  free (this->prop);
  free (this);

  *thisp = NULL;
}
