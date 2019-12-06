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

private void __init_ext__ (Class (ed) *e, Type (ed) *this) {
  (void) e; (void) this;

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
