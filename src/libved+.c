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

private int __ex_com_info__ (buf_t **thisp, rline_t *rl) {
  (void) thisp; (void) rl;
  int buf = Rline.arg.exists (rl, "buf");
  if (buf)
    Ed.append.toscratch ($myed, CLEAR, "");

  if (buf) {
    Ed.append.toscratch ($myed, DONOT_CLEAR, Buf.get.info (*thisp));
    // goto theend;
  }

//theend:
  if (buf)
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
  Ed.append.command_arg (this, "@info", "--buf", 10);
  Ed.set.rline_cb (this, __ex_rline_cb__);
}

private void __init_ext__ (Class (ed) *e, Type (ed) *this) {
  (void) e; (void) this;

  __ex_add_rline_commands__ (this);

#if HAS_REGEXP
  Re.exec = ext_re_exec;
  Re.parse_substitute =ext_re_parse_substitute;
  Re.compile = ext_re_compile;
#endif

#if HAS_SHELL_COMMANDS
  e->self.sh.popen = ext_ed_sh_popen;
#endif

#if HAS_USER_EXTENSIONS
  __init_usr__ (this);
#endif

#if HAS_LOCAL_EXTENSIONS
  __init_local__ (this);
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
