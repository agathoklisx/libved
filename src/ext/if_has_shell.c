/* this unit is no guarded */

#include "../modules/proc/proc.c"

private int proc_output_to_stdout (buf_t *this, fp_t *fp) {
  (void) this;
  char *line = NULL;
  size_t len = 0;
  while (-1 isnot getline (&line, &len, fp->fp))
    fprintf (stdout, "%s\r", line);
  ifnot (NULL is line) free (line);
  return 0;
}

private int my_ed_sh_popen (ed_t *ed, buf_t *buf, char *com,
  int redir_stdout, int redir_stderr, PopenRead_cb read_cb) {

  int retval = NOTOK;
  proc_t *this = proc_new ();
  $my(read_stderr) =  redir_stderr;
  $my(read_stdout) = 1;
  $my(dup_stdin) = 0;
  $my(buf) = buf;
  ifnot (NULL is read_cb)
    $my(read) = read_cb;
  else
    if (redir_stdout)
      $my(read) = Buf.read.from_fp;
    else
      $my(read) = proc_output_to_stdout;

  proc_parse (this, com);
  term_t *term = Ed.get.term (ed);

  ifnot (redir_stdout)
    Term.reset (term);

  if (NOTOK is proc_open (this)) goto theend;
  proc_read (this);
  retval = proc_wait (this);

theend:
  ifnot (redir_stdout) {
    Term.set_mode (term, 'r');
    Input.get (term);
    Term.set (term);
  }

  proc_free (this);
  win_t *w = Ed.get.current_win (ed);
  int idx = Win.get.current_buf_idx (w);
  Win.set.current_buf (w, idx, DONOT_DRAW);
  Win.draw (w);
  return retval;
}

/*
#ifdef HAS_SHELL_COMMANDS
  Ed.sh.popen = my_ed_sh_popen;
#endif
 */
