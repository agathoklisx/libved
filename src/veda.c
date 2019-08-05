/* A simple utility that utilizes the library by linking against. */

/* Some notes below might be outdated. The interface (the actual
 * structure) won't change much, but the way it does things might.
 * The way that someone access the structure is not yet guaranteed.
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
#define String  E->String.self
#define Rline   E->Rline.self
#define Error   E->Error.self
#define Term    E->Term.self
#define Input   E->Input.self
#define File    E->File.self
#define Buf     E->Buf.self
#define Win     E->Win.self
#define Msg     E->Msg.self
#define Dir     E->Dir.self
#define Re      E->Re.self

#define $myed E->current

/* Here, we enable regexp support by overiding structure fields, the
 * interface is exactly the same */
#ifdef HAS_REGEXP
#include "ext/if_has_regexp.c"
#endif /* HAS_REGEXP */

/* Here we enable the :r! cmd and :!cmd commands */
#ifdef HAS_SHELL_COMMANDS
#include "ext/if_has_shell.c"
#endif   /* HAS_SHELL_COMMANDS */

#include "handlers/sigwinch_handler.c"
#include "handlers/alloc_err_handler.c"

#ifdef HAS_USER_EXTENSIONS
#include "usr/usr_libved.c"
#endif

int main (int argc, char **argv) {
  /* I do not know the way to read from stdin and at the same time to
   * initialize and use the terminal state, when we are the end of the pipe */
  if (0 is isatty (fileno (stdout)) or 0 is isatty (fileno (stdin))) {
    tostderr ("Not a controlled terminal\n");
    exit (1);
  }

  setlocale (LC_ALL, "");

  AllocErrorHandler = __alloc_error_handler__;

  ++argv; --argc;

  if (NULL is (E = __init_ved__ ()))
    return 1;

  ed_t *this = E->current;

#ifdef HAS_REGEXP
  Re.exec = my_re_exec;
  Re.parse_substitute = my_re_parse_substitute;
  Re.compile = my_re_compile;
#endif

#ifdef HAS_SHELL_COMMANDS
  Ed.sh.popen = my_ed_sh_popen;
#endif

#ifdef HAS_USER_EXTENSIONS
  __init_usr__ (this);
#endif

#ifdef HAS_HISTORY
#ifdef VED_DATA_DIR
  Ed.history.read (this, VED_DATA_DIR);
#endif
#endif

/* at the begining at least a win_t type is allocated */
  win_t *w = Ed.get.current_win (this);

  ifnot (argc) {
    /* just create a new empty buffer and append it to its
     * parent win_t to the frame zero */
    buf_t *buf = Win.buf_new (w, NULL, 0, 0);
    Win.append_buf (w, buf);
  } else
    /* else create a new buffer for every file in the argvlist
     * (assumed files for simplification without an arg-parser) */
    for (int i = 0; i < argc; i++) {
      buf_t *buf = Win.buf_new (w, argv[i], 0, 0);
      Win.append_buf (w, buf);
    }

  /* set the first indexed name in the argvlist, as current */
  Win.set.current_buf (w, 0);

  int retval = 0;
  signal (SIGWINCH, sigwinch_handler);

  for (;;) {
    buf_t *buf = Ed.get.current_buf (this);
    /* main loop */
    retval = Ed.main (this, buf);

    /* here the user suspended its editor instance, with CTRL-j */
    if (Ed.get.state (this) & ED_SUSPENDED) {
      if (E->num_items is 1) {
        /* as an example, we simply create another independed instance */
        this = Ed.new (E, 1);
        w = Ed.get.current_win (this);
        buf = Win.buf_new (w, NULL, 0, 0);
        Win.append_buf (w, buf);
        Win.set.current_buf (w, 0);
      } else {
        /* else jump to the next or prev */
        this = Ed.get.prev (this);
      }
    } else break;
  }

#ifdef HAS_HISTORY
#ifdef VED_DATA_DIR
  Ed.history.write (this, VED_DATA_DIR);
#endif
#endif

  __deinit_ved__ (E);

/* the end */
  return retval;
}
