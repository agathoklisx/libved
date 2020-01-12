#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <pty.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <locale.h>
#include <errno.h>

#include "libved.h"
#include "libved+.h"

public Class (This) *__THIS__ = NULL;
public Self (This) *__SELF__ = NULL;
public Class (E) *__E__ = NULL;

#include "handlers/sigwinch_handler.c"
#include "handlers/alloc_err_handler.c"

#if HAS_USER_EXTENSIONS
  #include "usr/usr.c"
#endif

#if HAS_LOCAL_EXTENSIONS
  #include "local/local.c"
#endif

#if HAS_REGEXP
  #include "ext/if_has_regexp.c"
#endif

#if HAS_SHELL_COMMANDS
  #include "ext/if_has_shell.c"
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
      .head = e_get_head
    )
  );
  ((Self (This) *) this->self)->e = e;
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

  __THIS__ = AllocClass (This);
  __THIS__->__E__ = __e__;
  __THIS__->__E__->__THIS__ =  __THIS__;

  __init_self__ (__THIS__);
  __SELF__ = __THIS__->self;

  return __THIS__;
}

public void __deinit_this__ (Class (This) **thisp) {
  if (*thisp is NULL) return;

  Class (This) *this = *thisp;

  __deinit_ed__ (&this->__E__);

  free (__SELF__);
  free (this->prop);
  free (this);

  *thisp = NULL;
}
