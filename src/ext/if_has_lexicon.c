/* This function is called on Normal mode (on 'W' and activated via a menu),
 * or through the command line with the "~translate" command.
 * It performs a simple search (on the lexicon defined file) for 'word',
 * and then prints the matched lines to the scratch buffer (this buffer
 * can be closed with 'q' (as in a pager)):
 * requires the WORD_LEXICON_FILE to be defined with a way; my way is to
 * compile the distribution through a shell script, that invokes `make`
 * with my specific definitions.
 */

private int __translate_word__ (buf_t **thisp, char *word) {
  (void) thisp;
  char *lex = NULL;
  ed_t *ed = E.get.current (THIS_E);

#ifndef WORD_LEXICON_FILE
  Msg.error (ed, "%s(): lexikon has not been defined", __func__);
  return NOTOK;
#else
  lex = WORD_LEXICON_FILE;
#endif

  ifnot (File.exists (lex)) {
    Msg.error (ed, "%s(): lexicon has not been found", __func__);
    return NOTOK;
  }

  Msg.send_fmt (ed, COLOR_YELLOW, "translating [%s]", word);

  FILE *fp = fopen (lex, "r");
  if (NULL is fp) {
    Msg.error (ed, Error.string (ed, errno));
    return NOTOK;
  }

  size_t len = bytelen (word);
  char rgxp[len + 5];
  snprintf (rgxp, len + 5, "(?i)%s", word);

  regexp_t *re = Re.new (rgxp, 0, RE_MAX_NUM_CAPTURES, Re.compile);
  char *line = NULL;
  int nread;
  int match = 0;

  Ed.append.toscratch (ed, CLEAR, word);
  Ed.append.toscratch (ed, DONOT_CLEAR, "=================");

  len = 0;
  while (-1 isnot (nread = getline (&line, &len, fp)))
    if (0 <= Re.exec (re, line, nread)) {
      match++;
      Ed.append.toscratch (ed, DONOT_CLEAR, Cstring.trim.end (line, '\n'));
      Re.reset_captures (re);
    }

  fclose (fp);
  Re.free (re);
  if (line isnot NULL) free (line);

  return match;
}

private int __edit_lexicon__ (buf_t **thisp) {
  buf_t *this = *thisp;
  char *lex = NULL;

#ifndef WORD_LEXICON_FILE
  ed_t *ed = E.get.current (THIS_E);
  Msg.error (ed, "%s(): lexikon has not been defined", __func__);
  return NOTOK;
#else
  lex = WORD_LEXICON_FILE;
#endif

  win_t *w = Buf.get.parent (this);
  Win.edit_fname (w, thisp, lex, 0, 0, 1, 0);
  return OK;
}
