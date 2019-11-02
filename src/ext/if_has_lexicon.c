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

#ifndef WORD_LEXICON_FILE
  Msg.error ($myed, "%s(): lexikon has not been defined", __func__);
  return NOTOK;
#else
  lex = WORD_LEXICON_FILE;
#endif

  Msg.send_fmt ($myed, COLOR_YELLOW, "translating [%s]", word);

  ifnot (File.exists (lex)) {
    Msg.error ($myed, "%s(): lexicon has not been found", __func__);
    return NOTOK;
  }

  FILE *fp = fopen (lex, "r");
  if (NULL is fp) {
    Msg.error ($myed, Error.string ($myed, errno));
    return NOTOK;
  }

  size_t len = bytelen (word);
  char rgxp[len + 5];
  snprintf (rgxp, len + 5, "(?i)%s", word);

  regexp_t *re = Re.new (rgxp, 0, RE_MAX_NUM_CAPTURES, Re.compile);
  char *line = NULL;
  int nread;
  int match = 0;

  Ed.append.toscratch ($myed, CLEAR, word);
  Ed.append.toscratch ($myed, DONOT_CLEAR, "=================");

  len = 0;
  while (-1 isnot (nread = getline (&line, &len, fp)))
    if (0 <= Re.exec (re, line, nread)) {
      match++;
      Ed.append.toscratch ($myed, DONOT_CLEAR, Cstring.trim.end (line, '\n'));
      Re.reset_captures (re);
    }

  fclose (fp);
  Re.free (re);
  if (line isnot NULL) free (line);

  return match;
}
