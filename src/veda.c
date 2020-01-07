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
#include <sys/stat.h>
#include <errno.h>

#include <libved.h>
#include <libved+.h>

static const char *const usage[] = {
  "veda [options] [filename]",
  NULL,
};

int main (int argc, char **argv) {
  /* I do not know the way to read from stdin and at the same time to
   * initialize and use the terminal state, when we are the end of the pipe */
  if (0 is isatty (fileno (stdout)) or 0 is isatty (fileno (stdin))) {
    tostderr ("Not a controlled terminal\n");
    exit (1);
  }

  setlocale (LC_ALL, "");

  AllocErrorHandler = __alloc_error_handler__;

  char *ftype = NULL;
  int filetype = FTYPE_DEFAULT;
  int autosave = 0;
  int backupfile = 0;
  char *backup_suffix = NULL;
  int linenr = 0;
  int column = 1;
  int num_win = 1;

  struct argparse_option options[] = {
    OPT_HELP (),
    OPT_GROUP("Options:"),
    OPT_INTEGER('+', "line-nr", &linenr, "start at line number", NULL, 0, SHORT_OPT_HAS_NO_DASH),
    OPT_INTEGER(0, "column", &column, "set pointer at column", NULL, 0, 0),
    OPT_INTEGER(0, "num-win", &num_win, "create new [num] windows", NULL, 0, 0),
    OPT_STRING(0, "ftype", &ftype, "set the file type", NULL, 0, 0),
    OPT_INTEGER(0, "autosave", &autosave, "interval time in minutes to autosave buffer", NULL, 0, 0),
    OPT_BOOLEAN(0, "backupfile", &backupfile, "backup file at the initial reading", NULL, 0, 0),
    OPT_STRING(0, "backup-suffix", &backup_suffix, "backup suffix (default: ~)", NULL, 0, 0),
    OPT_END(),
  };


  struct argparse argparse;
  argparse_init (&argparse, options, usage, 0);
  argc = argparse_parse (&argparse, argc, (const char **) argv);

  if (argc is -1) return 1;

  if (NULL is (__E__ = __init_ed__ (MYNAME)))
    return 1;

  E.set.at_exit_cb (__E__, __deinit_ext__);

  num_win = (num_win < argc ? num_win : argc);

  ed_t *this = E.new (__E__, QUAL(ED_INIT, .num_win = num_win, .init_cb = __init_ext__));

  filetype = Ed.syn.get_ftype_idx (this, ftype);

  win_t *w = Ed.get.current_win (this);

  ifnot (argc) {
    /* just create a new empty buffer and append it to its
     * parent win_t to the frame zero */
    buf_t *buf = Win.buf.new (w, BUF_INIT_QUAL(.ftype = filetype, .autosave = autosave));
    Win.append_buf (w, buf);
  } else {
    int first_idx = Ed.get.current_win_idx (this);
    int widx = first_idx;
    int l_num_win = num_win;
    /* else create a new buffer for every file in the argvlist */
    for (int i = 0; i < argc; i++) {
      buf_t *buf = Win.buf.new (w, QUAL(BUF_INIT,
        .fname = argv[i],
        .ftype = filetype,
        .at_frame = FIRST_FRAME,
        .at_linenr = linenr,
        .at_column = column,
        .backupfile = backupfile,
        .backup_suffix = backup_suffix,
        .autosave = autosave));

      Win.append_buf (w, buf);

      if (--l_num_win > 0)
        w = Ed.set.current_win (this, ++widx);
    }

    w = Ed.set.current_win (this, first_idx);
  }

  /* set the first indexed name in the argvlist, as current */
  Win.set.current_buf (w, 0, DRAW);

  int retval = 0;
  signal (SIGWINCH, sigwinch_handler);

  for (;;) {
    buf_t *buf = Ed.get.current_buf (this);

    retval = E.main (__E__, buf);

    int state = E.get.state (__E__);

    if ((state & ED_EXIT))
      break;

    if ((state & ED_SUSPENDED)) {
      if (E.get.num (__E__) is 1) {
        /* as an example, we simply create another independed instance */
        this = E.new (__E__, QUAL(ED_INIT, .init_cb = __init_ext__));

        w = Ed.get.current_win (this);
        buf = Win.buf.new (w, BUF_INIT_QUAL());
        Win.append_buf (w, buf);
        Win.set.current_buf (w, 0, DRAW);
      } else {
        /* else jump to the next or prev */
        this = E.set.current (__E__, E.get.prev_idx (__E__));
        w = Ed.get.current_win (this);
      }

      continue;
    }

    break;
  }

  __deinit_ed__ (&__E__);

/* the end */
  return retval;
}
