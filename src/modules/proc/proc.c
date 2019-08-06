#include <stdarg.h>
#include <sys/wait.h>

NewProp (proc,
  pid_t  pid;
  char **argv;
   int   argc;
   int   sys_errno;
   int   is_bg;
   int   read_stdout;
   int   read_stderr;
   int   dup_stdin;
   int   stdout_fds[2];
   int   stderr_fds[2];
   int   stdin_fds[2];
   int   status;
   PopenRead_cb read;
   buf_t *buf;
);

NewType (proc,
  Prop (proc) *prop;
);

NewSelf (proc,
  proc_t *(*new) (void);
  void
    (*free) (proc_t *),
    (*free_argv) (proc_t *);
  pid_t (*wait) (proc_t *);
);

NewClass (proc,
  Self (proc) self;
);

#define PIPE_READ_END  0
#define PIPE_WRITE_END 1

private void proc_free_argv (proc_t *this) {
  for (int i = 0; i <= $my(argc); i++)
    free ($my(argv)[i]);
  free ($my(argv));
}

private void proc_free (proc_t *this) {
  ifnot (this) return;
  proc_free_argv (this);
  free (this->prop);
  free (this);
}

private proc_t *proc_new (void) {
  proc_t *p = AllocType (proc);
  p->prop = AllocProp (proc);
  return p;
}

private int proc_wait (proc_t *this) {
  if (-1 is $my(pid)) return NOTOK;
  waitpid ($my(pid), &$my(status), WNOHANG|WUNTRACED);
  return $my(status);
}

private int proc_read (proc_t *this) {
  int retval = NOTOK;

  if ($my(read_stdout)) {
    if ($my(read) isnot NULL and $my(buf) isnot NULL) {
      fp_t fp = (fp_t) {.fp = fdopen ($my(stdout_fds)[PIPE_READ_END], "r")};
      retval = $my(read) ($my(buf), &fp);
      fclose (fp.fp);
    }
  }

  if ($my(read_stderr)) {
    if ($my(read) isnot NULL and $my(buf) isnot NULL) {
      fp_t fp = (fp_t) {.fp = fdopen ($my(stderr_fds)[PIPE_READ_END], "r")};
      retval = $my(read) ($my(buf), &fp);
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
    memcpy ($my(argv)[$my(argc)-1], tokbeg, len);
    $my(argv)[$my(argc)-1][len] = '\0';
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

public proc_T __init_proc__ (void) {
  return ClassInit (proc,
    .self = SelfInit (proc,
      .new = proc_new,
      .free = proc_free,
      .wait = proc_wait,
    )
  );
}
