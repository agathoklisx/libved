#include <stdarg.h>
#include <sys/wait.h>
#include <pty.h>

private void proc_free_argv (proc_t *this) {
  if (NULL is $my(argv))
    return;

  for (int i = 0; i <= $my(argc); i++)
    free ($my(argv)[i]);
  free ($my(argv));
}

private void proc_free (proc_t *this) {
  ifnot (this) return;
  proc_free_argv (this);
  if (NULL isnot $my(stdin_buf))
    free ($my(stdin_buf));

  free (this->prop);
  free (this);
}

private int proc_output_to_stream (buf_t *this, FILE *stream, fp_t *fp) {
  (void) this;
  char *line = NULL;
  size_t len = 0;
  while (-1 isnot getline (&line, &len, fp->fp))
    fprintf (stream, "%s\r", line);

  ifnot (NULL is line) free (line);
  return 0;
}

private proc_t *proc_new (void) {
  proc_t *this= AllocType (proc);
  $myprop = AllocProp (proc);
  $my(pid) = -1;
  $my(stdin_buf) = NULL;
  $my(dup_stdin) = 0;
  $my(read_stdout) = 0;
  $my(read_stderr) = 0;
  $my(read) = proc_output_to_stream;
  $my(argc) = 0;
  $my(argv) = NULL;
  $my(reset_term) = 0;
  $my(prompt_atend) = 1;
  return this;
}

private int proc_wait (proc_t *this) {
  if (-1 is $my(pid)) return NOTOK;
  $my(status) = 0;
  waitpid ($my(pid), &$my(status), 0);
 // waitpid ($my(pid), &$my(status), WNOHANG|WUNTRACED);
  if (WIFEXITED ($my(status)))
    return WEXITSTATUS ($my(status));
  return -1;
}

private int proc_read (proc_t *this) {
  int retval = NOTOK;

  if ($my(read_stdout)) {
    if ($my(read) isnot NULL and $my(buf) isnot NULL) {
      fp_t fp = (fp_t) {.fp = fdopen ($my(stdout_fds)[PIPE_READ_END], "r")};
      retval = $my(read) ($my(buf), stdout, &fp);
      fclose (fp.fp);
    }
  }

  if ($my(read_stderr)) {
    if ($my(read) isnot NULL and $my(buf) isnot NULL) {
      fp_t fp = (fp_t) {.fp = fdopen ($my(stderr_fds)[PIPE_READ_END], "r")};
      retval = $my(read) ($my(buf), stderr, &fp);
      fclose (fp.fp);
    }
  }

  return retval;
}

private char **proc_parse (proc_t *this, char *com) {
  char *sp = com;

  char *tokbeg;
  $my(argv) = Alloc (sizeof (char *));
  size_t len;

  while (*sp) {
    while (*sp and *sp is ' ') sp++;
    ifnot (*sp) break;

    if (*sp is '&' and *(sp + 1) is 0) {
      $my(is_bg) = 1;
      break;
    }

    tokbeg = sp;

    if (*sp is '"') {
      sp++;
      tokbeg++;

parse_quoted:
      while (*sp and *sp isnot '"') sp++;
      ifnot (*sp) goto theerror;
      if (*(sp - 1) is '\\') goto parse_quoted;
      len = (size_t) (sp - tokbeg);
      sp++;
      goto add_arg;
    }

    while (*sp and *sp isnot ' ') sp++;
    ifnot (*sp) {
      if (*(sp - 1) is '&') {
        $my(is_bg) = 1;
        sp--;
      }
    }

    len = (size_t) (sp - tokbeg);

add_arg:
    $my(argc)++;
    $my(argv) = Realloc ($my(argv), sizeof (char *) * ($my(argc) + 1));
    $my(argv)[$my(argc)-1] = Alloc (len + 1);
    Cstring.cp ($my(argv)[$my(argc)-1], len + 1, tokbeg, len);
    sp++;
  }

  $my(argv)[$my(argc)] = (char *) NULL;
  return $my(argv);

theerror:
  proc_free_argv (this);
  return NULL;
}

private void proc_close_pipe (int num, ...) {
  va_list ap;
  int *p;
  int is_open;
  va_start (ap, num);
  is_open = va_arg (ap, int);
  p = va_arg (ap, int *);
  if (is_open) {
    close (p[0]);
    close (p[1]);
  }
  va_end (ap);
}

private int proc_open (proc_t *this) {
  if (NULL is this) return NOTOK;
  ifnot ($my(argc)) return NOTOK;

  if ($my(dup_stdin)) {
    if (-1 is pipe ($my(stdin_fds)))
      return NOTOK;
  }

  if ($my(read_stdout)) {
    if (-1 is pipe ($my(stdout_fds))) {
      proc_close_pipe (1, $my(dup_stdin), $my(stdin_fds));
      return NOTOK;
    }
  }

  if ($my(read_stderr)) {
    if (-1 is pipe ($my(stderr_fds))) {
      proc_close_pipe (1,
          $my(dup_stdin), $my(stdin_fds),
          $my(read_stdout), $my(stdout_fds));
      return NOTOK;
    }
  }

  if (-1 is ($my(pid) = fork ())) {
    proc_close_pipe (1,
       $my(dup_stdin), $my(stdin_fds),
       $my(read_stdout), $my(stdout_fds),
       $my(read_stderr), $my(stderr_fds));
    return NOTOK;
  }

  /* this code seems to prevent from execution many of interactive applications
   * which is desirable (many, but not all), but this looks to work for commands
   * that need to be executed and in the case of :r! to read from standard output
   */

  ifnot ($my(pid)) {
    setpgid (0, 0);
    setsid ();
    if ($my(read_stderr)) {
      dup2 ($my(stderr_fds)[PIPE_WRITE_END], fileno (stderr));
      close ($my(stderr_fds)[PIPE_READ_END]);
    }

    if ($my(read_stdout)) {
      close ($my(stdout_fds)[PIPE_READ_END]);
      dup2 ($my(stdout_fds)[PIPE_WRITE_END], fileno (stdout));
    }

    if ($my(dup_stdin)) {
      dup2 ($my(stdin_fds)[PIPE_READ_END], STDIN_FILENO);
      ifnot (NULL is $my(stdin_buf))
        write ($my(stdin_fds)[PIPE_WRITE_END], $my(stdin_buf), $my(stdin_buf_size));

      close ($my(stdin_fds)[PIPE_WRITE_END]);
    }

    execvp ($my(argv)[0], $my(argv));
    $my(sys_errno) = errno;
    _exit (1);
  }

  if ($my(dup_stdin)) close ($my(stdin_fds)[PIPE_READ_END]);
  if ($my(read_stdout)) close ($my(stdout_fds)[PIPE_WRITE_END]);
  if ($my(read_stderr)) close ($my(stderr_fds)[PIPE_WRITE_END]);

  return $my(pid);
}

private int proc_exec (proc_t *this, char *com) {
  int retval = 0;

  if ($my(reset_term))
    Term.reset ($my(term));

  proc_parse (this, com);

  if (NOTOK is proc_open (this)) goto theend;

  proc_read (this);

  retval = proc_wait (this);

theend:
  if($my(reset_term))
    if ($my(prompt_atend)) {
      Term.set_mode ($my(term), 'r');
      Input.get ($my(term));
      Term.set ($my(term));
    }

  return retval;
}

public proc_T __init_proc__ (void) {
  return ClassInit (proc,
    .self = SelfInit (proc,
      .new = proc_new,
      .free = proc_free,
      .wait = proc_wait,
      .parse = proc_parse,
      .read = proc_read,
      .exec = proc_exec
    )
  );
}
