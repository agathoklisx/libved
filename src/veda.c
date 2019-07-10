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

/* The only static variable, needs to be available on handler[s] */
static ed_T *E = NULL;

/* The ideal is to never need more than those two macros */
#define Ed E->self
#undef My
#define My(__C__) E->__C__.self

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

int main (int argc, char **argv) {
  /* I do not know the way to read from stdin and at the same time to
   * initialize and use the terminal state, when we are the end of the pipe */
  if (0 is isatty (fileno (stdout)) or 0 is isatty (fileno (stdin))) {
    fprintf (stderr, "Not a controlled terminal\n");
    exit (1);
  }

  setlocale (LC_ALL, "");

  AllocErrorHandler = __alloc_error_handler__;

  ++argv; --argc;

/* initialize root structure, accessible from now on with: 
 * Ed.method[.submethod] ([[arg],...])
 * Ed.Class.self.method ([[arg],...]) // edsubclass class
 * My(Class).method ([[arg],...])     // for short
 */
  if (NULL is (E = __init_ved__ ()))
    return 1;

/* overide */
#ifdef HAS_REGEXP
  My(Re).exec = my_re_exec;
  My(Re).parse_substitute = my_re_parse_substitute;
  My(Re).compile = my_re_compile;
#endif

#ifdef HAS_SHELL_COMMANDS
  Ed.sh.popen = my_ed_sh_popen;
#endif

  ed_t *this = E->current;
/* why we can't use self here like in the library? Because
 * almost all of the types are opaque pointers */

/* at the begining at least a win_t type is allocated */
  win_t *w = Ed.get.current_win (this);

  ifnot (argc) {
    /* just create a new empty buffer and append it to its
     * parent win_t to the frame zero */
    buf_t *buf = My(Win).buf_new (w, NULL, 0, 0);
    My(Win).append_buf (w, buf);
  } else
    /* else create a new buffer for every file in the argvlist
     * (assumed files for simplification without an arg-parser) */
    for (int i = 0; i < argc; i++) {
      buf_t *buf = My(Win).buf_new (w, argv[i], 0, 0);
      My(Win).append_buf (w, buf);
    }

  /* set the first indexed name in the argvlist, as current */
  My(Win).set.current_buf (w, 0);

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
        Ed.new (E, 1);
        this = E->current;
        w = Ed.get.current_win (this);
        buf = My(Win).buf_new (w, NULL, 0, 0);
        My(Win).append_buf (w, buf);
        My(Win).set.current_buf (w, 0);
        /* hope i got them right! surely this needs an improvement */
      } else {
        /* else jump to the next or prev */
        ed_t *ed = Ed.get.next (this);
        if (NULL is ed) ed = Ed.get.prev (this);
        this = ed;
      }
    } else goto theend;
  }

theend:
  __deinit_ved__ (E);


/* the end */
  return retval;
}
