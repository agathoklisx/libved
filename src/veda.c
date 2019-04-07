#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include <libved.h>

ed_T *E;

#undef My
#define Ed E->self
#define My(__C__) E->__C__.self

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

  exit (1);
}

int main (int argc, char **argv) {

  AllocErrorHandler = __alloc_error_handler__;

  ++argv; --argc;

  E = __init_ved__ ();

  if (NULL is E) return 1;
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
