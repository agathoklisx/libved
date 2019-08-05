#ifndef SYS_NAME
#error "SYS_NAME is not defined"
#else
#if defined(SYS_IS_LINUX)
#define SYS_BATTERY_DIR "/sys/class/power_supply"
#else
#define SYS_BATTERY_DIR
#endif /* SYS_IS_LINUX */
#endif /* SYS_NAME */

#include <sys/stat.h> /* for mkdir() */

/* user sample extension[s] (mostly personal) and basic system command[s],
that since explore[s] the API, this unit is also a vehicle to understand
the needs and establish this application layer */


          /* user defined commands and|or actions */

/* this function that extends normal mode performs a simple search on a
 * lexicon defined file for 'word', and then prints the matched lines to
 * the scratch buffer (this buffer can be closed with 'q' (as in a pager)):
 * requires the WORD_LEXICON_FILE to be defined with a way; my way is to
 * compile the distribution through a shell script, that invokes `make`
 * with my specific definitions
 */
private int ved_translate_word (buf_t **thisp, char *word) {
  (void) thisp;
  char *lex = NULL;

#ifndef WORD_LEXICON_FILE
  Msg.error ($myed, "%s(): lexikon has not been defined", __func__);
  return NOTOK;
#else
  lex = WORD_LEXICON_FILE;
#endif

  Msg.send_fmt ($myed, COLOR_YELLOW, "translating %s", word);

  ifnot (File.exists (lex)) {
    Msg.error ($myed, "%s(): lexicon has not been found", __func__);
    return NOTOK;
  }

  FILE *fp = fopen (lex, "r");
  if (NULL is fp) {
    Msg.error ($myed, Error.string ($myed, errno));
    return NOTOK;
  }

  regexp_t *re = Re.new (word, 0, RE_MAX_NUM_CAPTURES, Re.compile);
  size_t len;
  char *line = NULL;
  int nread;
  int match = 0;

  toscratch ($myed, "", 1);

  while (-1 isnot (nread = getline (&line, &len, fp)))
    if (0 <= Re.exec (re, line, nread)) {
      match++;
      toscratch ($myed, line, 0);
      Re.reset_captures (re);
    }

  fclose (fp);
  Re.free (re);
  if (line isnot NULL) free (line);

  return match;
}

/* the callback function that is called on 'W' in normal mode */
private int __word_actions_cb__ (buf_t **thisp, char *word, utf8 c) {
  int retval = 0;

  switch (c) {
    case 't':
      ifnot (retval = ved_translate_word (thisp, word))
        Msg.send ($myed, COLOR_RED, "Nothing matched the pattern");
      else if (NOTOK isnot retval)
        Ed.scratch ($myed, thisp, 0);
      return 0;

    default:
      (void) thisp;
      break;
   }

  return 0;
}

/* this function extends standard defined commands with a `battery command
 * this prints to the message line (through an over simplistic way and only
 * on Linux systems) the battery status and capacity */
private int sys_battery_info (char *buf, int should_print) {
  int retval = NOTOK;

  /* use the SYS_NAME defined in the Makefile and avoid uname() for now */
  ifnot (Cstring.eq ("Linux", SYS_NAME)) {
    Msg.error ($myed, "battery function implemented for Linux");
    return NOTOK;
  }

  dirlist_t *dlist = Dir.list (SYS_BATTERY_DIR, 0);
  char *cap = NULL;
  char *status = NULL;

  if (NULL is dlist) return NOTOK;

  vstring_t *it = dlist->list->head;
  while (it) {
    ifnot (Cstring.cmp_n ("BAT", it->data->bytes, 3)) goto foundbat;
    it = it->next;
    }

  goto theend;

/* funny goto's (i like them (the goto's i mean)) */
foundbat:;
  /* some maybe needless verbosity */
  char dir[64];
  snprintf (dir, 64, "%s/%s/", SYS_BATTERY_DIR, it->data->bytes);
  size_t len = bytelen (dir);
  strcpy (dir + len, "capacity");
  FILE *fp = fopen (dir, "r");
  if (NULL is fp) goto theend;

  size_t clen = 0;
  ssize_t nread = getline (&cap, &clen, fp);
  if (-1 is nread) goto theend;

  cap[nread - 1] = '\0';
  fclose (fp);

  dir[len] = '\0';
  strcpy (dir + len, "status");
  fp = fopen (dir, "r");
  if (NULL is fp) goto theend;

/* here clen it should be zero'ed because on the second call the application
 * segfaults (compiled with gcc and clang and at the first call with tcc);
 * this is when the code tries to free both char *variables arguments to getline();
 * this is as (clen) possibly interpeted as the length of the buffer
 */
  clen = 0;

  nread = getline (&status, &clen, fp);
  if (-1 is nread) goto theend;

  status[nread - 1] = '\0';
  fclose (fp);

  retval = OK;

  if (should_print)
    Msg.send_fmt ($myed, COLOR_YELLOW, "[Battery is %s, remaining %s%%]",
        status, cap);

  ifnot (NULL is buf) snprintf (buf, 64, "[Battery is %s, remaining %s%%]",
      status, cap);

theend:
  ifnot (NULL is cap) free (cap);
  ifnot (NULL is status) free (status);
  dlist->free (dlist);
  return retval;
}

private void __add_rline_user_commands__ (ed_t *this) {
/* user defined commands can begin with '~': associated in mind with '~' as $HOME */
  char *commands[2] = {"~battery", NULL};
  int num_args[] = {0, 0}; int flags[] = {0, 0};
  Ed.append.rline_commands (this, commands, 1, num_args, flags);
}

private int sys_mkdir (char *dir, mode_t mode, int verbose) {
  ifnot (mkdir (dir, mode)) {
    if (verbose)
      Msg.send_fmt ($myed, COLOR_YELLOW, "created directory `%s', with mode: 0%o",
          dir, mode);
    return OK;
  }

/* to do: handle errno */
  return NOTOK;
}

private void __add_rline_sys_commands__ (ed_t *this) {
/* sys defined commands can begin with '`': associated with shell syntax */
  char *commands[2] = {"`mkdir", NULL};
  int num_args[] = {2, 0}; int flags[] = {RL_ARG_FILENAME|RL_ARG_VERBOSE, 0};
  Ed.append.rline_commands (this, commands, 1, num_args, flags);
}

/* this is the callback function that is called on the extended by
 * the user commands
 */
private int __rline_cb__ (buf_t **thisp, rline_t *rl, utf8 c) {
  (void) thisp; (void) c;
  int retval = NOTOK;
  string_t *com = Rline.get.command (rl);

  if (Cstring.eq (com->bytes, "~battery"))
    retval = sys_battery_info (NULL, 1);
  else if (Cstring.eq (com->bytes, "`mkdir")) {
    vstr_t *dirs = Rline.get.arg_fnames (rl, 1);
    if (NULL is dirs) goto theend;
    int is_verbose = Rline.has.arg (rl, "verbose");
    retval = sys_mkdir (dirs->tail->data->bytes, S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH, is_verbose);
    Vstring.free (dirs);
  }

theend:
  String.free (com);
  return retval;
}

private void __add_rline_commands__ (ed_t *this) {
  __add_rline_sys_commands__ (this);
  __add_rline_user_commands__ (this);
  Ed.set.rline_cb (this, __rline_cb__);
}

private void __add_word_actions__ (ed_t *this) {
  utf8 chars[] = {'t'};
  char actions[] = "translate word\n";
  Ed.set.word_actions (this, chars, 1, actions, __word_actions_cb__);
}

private void __init_usr__ (ed_t *this) {
  /* as a first sample, extend the actions on current word, triggered by 'W' */
  __add_word_actions__ (this);
  /* extend commands */
  __add_rline_commands__ (this);
}
