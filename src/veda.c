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
#include <sys/stat.h>
#include <errno.h>

#include <libved.h>
#include <libved+.h>

static const char *const usage[] = {
  "veda [options] [filename]",
  NULL,
};

int main (int argc, char **argv) {
  if (NULL is __init_this__ ())
    return 1;

  char
    *load_file = NULL,
    *ftype = NULL,
    *backup_suffix = NULL,
    *exec_com = NULL;

  int
    retval = 0,
    exit = 0,
    filetype = FTYPE_DEFAULT,
    autosave = 0,
    backupfile = 0,
    linenr = 0,
    column = 1,
    num_win = 1;

  argparse_option_t options[] = {
    OPT_HELP (),
    OPT_GROUP("Options:"),
    OPT_INTEGER('+', "line-nr", &linenr, "start at line number", NULL, 0, SHORT_OPT_HAS_NO_DASH),
    OPT_INTEGER(0, "column", &column, "set pointer at column", NULL, 0, 0),
    OPT_INTEGER(0, "num-win", &num_win, "create new [num] windows", NULL, 0, 0),
    OPT_STRING(0, "ftype", &ftype, "set the file type", NULL, 0, 0),
    OPT_INTEGER(0, "autosave", &autosave, "interval time in minutes to autosave buffer", NULL, 0, 0),
    OPT_BOOLEAN(0, "backupfile", &backupfile, "backup file at the initial reading", NULL, 0, 0),
    OPT_STRING(0, "backup-suffix", &backup_suffix, "backup suffix (default: ~)", NULL, 0, 0),
    OPT_STRING(0, "exec-com", &exec_com, "execute command", NULL, 0, 0),
    OPT_STRING(0, "load-file", &load_file, "eval file", NULL, 0, 0),
    OPT_BOOLEAN(0, "exit", &exit, "exit", NULL, 0, 0),
    OPT_END()
  };

  argparse_t argparser;
  This(parse.arg.init, &argparser, options, usage, 0);
  argc = This(parse.arg.run, &argparser, argc, (const char **) argv);

  if (argc is -1) goto theend;

  E(set.at_exit_cb, __deinit_ext__);

  ed_t *this = NULL;
  win_t *w = NULL;

  if (load_file isnot NULL) {
    ifnot (OK is This(i.load_file, load_file))
      goto theend;

    signal (SIGWINCH, sigwinch_handler);

    this = E(get.current);
    w = Ed.get.current_win (this);
    goto theloop;
  }

  num_win = (num_win < argc ? num_win : argc);

  this = E(new, QUAL(ED_INIT, .num_win = num_win, .init_cb = __init_ext__));

  filetype = Ed.syn.get_ftype_idx (this, ftype);

  w = Ed.get.current_win (this);

  ifnot (argc) {
    /* just create a new empty buffer and append it to its
     * parent win_t to the frame zero */
    buf_t *buf = Win.buf.new (w, BUF_INIT_QUAL(.ftype = filetype, .autosave = autosave));
    Win.append_buf (w, buf);
  } else {
    int
      first_idx = Ed.get.current_win_idx (this),
      widx = first_idx,
      l_num_win = num_win;

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

  signal (SIGWINCH, sigwinch_handler);

  if (exec_com isnot NULL) {
    string_t *com = This(parse.command, exec_com);
    rline_t *rl = Rline.new_with (this, com->bytes);
    buf_t *buf = Ed.get.current_buf (this);
    retval = Rline.exec (rl, &buf);
    String.free (com);
  }

  if (exit) {
    if (retval <= NOTOK) retval = 1;
    goto theend;
  }

theloop:;
  for (;;) {
    buf_t *buf = Ed.get.current_buf (this);
    retval = E(main, buf);

    int state = E(get.state);

    if ((state & ED_EXIT))
      break;

    if ((state & ED_SUSPENDED)) {
      if (E(get.num) is 1) {
        /* as an example, we simply create another independed instance */
        this = E(new, QUAL(ED_INIT, .init_cb = __init_ext__));

        w = Ed.get.current_win (this);
        buf = Win.buf.new (w, BUF_INIT_QUAL());
        Win.append_buf (w, buf);
        Win.set.current_buf (w, 0, DRAW);
      } else {
        /* else jump to the next or prev */
        int prev_idx = E(get.prev_idx);
        this = E(set.current, prev_idx);
        w = Ed.get.current_win (this);
      }

      continue;
    }

    break;
  }

theend:
  __deinit_this__ (&__THIS__);
  return retval;
}
