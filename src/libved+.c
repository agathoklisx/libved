#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <pty.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

#include "libved.h"
#include "libved+.h"

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
  int
    buf = Rline.arg.exists (rl, "buf"),
    win = Rline.arg.exists (rl, "win"),
    ed  = Rline.arg.exists (rl, "ed");

  ifnot (buf + win + ed) buf = 1;

  Ed.append.toscratch ($myed, CLEAR, "");

  if (buf) {
    bufinfo_t *binfo = Buf.get.info.as_type (*thisp);
    string_t *sbinfo = __ex_buf_serial_info__ (binfo);
    Ed.append.toscratch ($myed, DONOT_CLEAR, sbinfo->bytes);
    String.free (sbinfo);
    Buf.free.info (*thisp, &binfo);
  }

  if (win) {
    win_t *cw = Ed.get.current_win ($myed);
    wininfo_t *winfo = Win.get.info.as_type (cw);
    string_t *swinfo = __ex_win_serial_info__ (winfo);
    Ed.append.toscratch ($myed, DONOT_CLEAR, swinfo->bytes);
    String.free (swinfo);
    Win.free_info (cw, &winfo);
  }

  if (ed) {
    edinfo_t *einfo = Ed.get.info.as_type ($myed);
    string_t *seinfo = __ex_ed_serial_info__ (einfo);
    Ed.append.toscratch ($myed, DONOT_CLEAR, seinfo->bytes);
    String.free (seinfo);
    Ed.free_info ($myed, &einfo);
  }

  Ed.scratch ($myed, thisp, NOT_AT_EOF);

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

private void __deinit_ext__ (ed_t *this) {
#if HAS_USER_EXTENSIONS
  __deinit_usr__ (this);
#endif

#if HAS_LOCAL_EXTENSIONS
  __deinit_local__ (this);
#endif
}
