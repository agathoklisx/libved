private int __validate_utf8_cb__ (vstr_t *unused, char *line, size_t len,
                                                      int lnr, void *obj) {
  (void) unused;
  int *retval = (int *) obj;
  char *message;
  int num_faultbytes;
  int cur_idx = 0;
  char *bytes = line;
  size_t orig_len = len;
  size_t index;

check_utf8:
  index = is_utf8 ((unsigned char *) bytes, len, &message, &num_faultbytes);

  ifnot (index) return OK;

  Ed.append.toscratch_fmt (E(get.current), DONOT_CLEAR,
      "--== Invalid UTF8 sequence ==-\n"
      "message: %s\n"
      "%s\nat line number %d, at index %zd, num invalid bytes %d\n",
      message, line, lnr, index + cur_idx, num_faultbytes);

  *retval = NOTOK;
  cur_idx += index + num_faultbytes;
  len = orig_len - cur_idx;
  bytes = line + cur_idx;
  num_faultbytes = 0;
  message = NULL;
  goto check_utf8;

  return *retval;
}
